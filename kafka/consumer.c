#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <librdkafka/rdkafka.h>
#include <tarantool/module.h>

#include <common.h>
#include <callbacks.h>
#include <queue.h>
#include <consumer_msg.h>

#include "consumer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Consumer poll thread
 */

static void *
consumer_poll_loop(void *arg) {
    set_thread_name("kafka_consumer");

    consumer_poller_t *poller = arg;
    event_queues_t *event_queues = rd_kafka_opaque(poller->rd_consumer);
    rd_kafka_message_t *rd_msg = NULL;
    rd_kafka_resp_err_t err = RD_KAFKA_RESP_ERR_NO_ERROR;
    int count = 0;
    int errors_count = 0;
    int should_stop = 0;

    while (true) {
        {
            pthread_mutex_lock(&poller->lock);

            should_stop = poller->should_stop;

            pthread_mutex_unlock(&poller->lock);

            if (should_stop) {
                break;
            }
        }

        {
            rd_msg = rd_kafka_consumer_poll(poller->rd_consumer, 1000);
            if (rd_msg != NULL) {
                err = rd_msg->err;
                if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                    // free rdkafka message instantly to prevent hang on close / destroy consumer
                    rd_kafka_message_destroy(rd_msg);
                    rd_msg = NULL;

                    error_callback(poller->rd_consumer, err, rd_kafka_err2str(err), event_queues);

                    errors_count++;
                    if (errors_count >= 50) {
                        // throttling calls with 100ms sleep when there are too many errors one by one
                        usleep(100000);
                    }
                } else {
                    msg_t *msg = new_consumer_msg(rd_msg);
                    // free rdkafka message instantly to prevent hang on close / destroy consumer
                    rd_kafka_message_destroy(rd_msg);
                    rd_msg = NULL;

                    pthread_mutex_lock(&event_queues->consume_queue->lock);

                    queue_lockfree_push(event_queues->consume_queue, msg);
                    count = event_queues->consume_queue->count;

                    pthread_mutex_unlock(&event_queues->consume_queue->lock);

                    errors_count = 0;
                    if (count >= 50000) {
                        // throttling calls with 100ms sleep when there are too many messages pending in queue
                        usleep(100000);
                    }
                }
            } else {
                // throttling calls with 100ms sleep
                usleep(100000);
            }
        }
    }

    pthread_exit(NULL);
}

static consumer_poller_t *
new_consumer_poller(rd_kafka_t *rd_consumer) {
    consumer_poller_t *poller = malloc(sizeof(consumer_poller_t));
    if (poller == NULL)
        return NULL;

    poller->rd_consumer = rd_consumer;
    poller->should_stop = 0;

    pthread_mutex_init(&poller->lock, NULL);
    pthread_attr_init(&poller->attr);
    pthread_attr_setdetachstate(&poller->attr, PTHREAD_CREATE_JOINABLE);
    int rc = pthread_create(&poller->thread, &poller->attr, consumer_poll_loop, (void *)poller);
    if (rc != 0) {
        free(poller);
        return NULL;
    }

    return poller;
}

static ssize_t
stop_poller(va_list args) {
    consumer_poller_t *poller = va_arg(args, consumer_poller_t *);
    pthread_mutex_lock(&poller->lock);

    poller->should_stop = 1;

    pthread_mutex_unlock(&poller->lock);

    pthread_join(poller->thread, NULL);

    return 0;
}

static void
stop_consumer_poller(consumer_poller_t *poller) {
    // stopping polling thread
    coio_call(stop_poller, poller);
}

static void
destroy_consumer_poller(consumer_poller_t *poller) {
    pthread_attr_destroy(&poller->attr);
    pthread_mutex_destroy(&poller->lock);
    free(poller);
}

/**
 * Consumer
 */

static inline consumer_t *
lua_check_consumer(struct lua_State *L, int index) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, index, consumer_label);
    if (consumer_p == NULL || *consumer_p == NULL)
        luaL_error(L, "Kafka consumer fatal error: failed to retrieve consumer from lua stack!");
    return *consumer_p;
}

int
lua_consumer_subscribe(struct lua_State *L) {
    if (lua_gettop(L) != 2 || !lua_istable(L, 2))
        luaL_error(L, "Usage: err = consumer:subscribe({'topic'})");

    consumer_t *consumer = lua_check_consumer(L, 1);

    if (consumer->topics == NULL) {
        consumer->topics = rd_kafka_topic_partition_list_new(lua_objlen(L, 1));
    }

    lua_pushnil(L);
    // stack now contains: -1 => nil; -2 => table; -3 => consumer
    while (lua_next(L, -2)) {
        // stack now contains: -1 => value; -2 => key; -3 => table; -4 => consumer
        const char *value = lua_tostring(L, -1);

        rd_kafka_topic_partition_list_add(consumer->topics, value, -1);

        // pop value, leaving original key
        lua_pop(L, 1);
        // stack now contains: -1 => key; -2 => table; -3 => consumer
    }
    // stack now contains: -1 => table; -2 => consumer

    rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer->rd_consumer, consumer->topics);
    if (err) {
        lua_pushstring(L, rd_kafka_err2str(err));
        return 1;
    }

    return 0;
}

int
lua_consumer_unsubscribe(struct lua_State *L) {
    if (lua_gettop(L) != 2 || !lua_istable(L, 2))
        luaL_error(L, "Usage: err = consumer:unsubscribe({'topic'})");

    consumer_t *consumer = lua_check_consumer(L, 1);

    if (consumer->topics == NULL) {
        return 0;
    }

    lua_pushnil(L);
    // stack now contains: -1 => nil; -2 => table; -3 => consumer
    while (lua_next(L, -2)) {
        // stack now contains: -1 => value; -2 => key; -3 => table; -4 => consumer
        const char *value = lua_tostring(L, -1);

        rd_kafka_topic_partition_list_del(consumer->topics, value, -1);

        // pop value, leaving original key
        lua_pop(L, 1);
        // stack now contains: -1 => key; -2 => table; -3 => consumer
    }
    // stack now contains: -1 => table; -2 => consumer

    if (consumer->topics->cnt > 0) {
        rd_kafka_resp_err_t err = rd_kafka_subscribe(consumer->rd_consumer, consumer->topics);
        if (err) {
            lua_pushstring(L, rd_kafka_err2str(err));
            return 1;
        }
    } else {
        rd_kafka_resp_err_t err = rd_kafka_unsubscribe(consumer->rd_consumer);
        if (err) {
            lua_pushstring(L, rd_kafka_err2str(err));
            return 1;
        }
    }

    return 0;
}

int
lua_consumer_tostring(struct lua_State *L) {
    consumer_t *consumer = lua_check_consumer(L, 1);
    lua_pushfstring(L, "Kafka Consumer: %p", consumer);
    return 1;
}

int
lua_consumer_poll_msg(struct lua_State *L) {
    if (lua_gettop(L) != 2)
        luaL_error(L, "Usage: msgs = consumer:poll_msg(msgs_limit)");

    consumer_t *consumer = lua_check_consumer(L, 1);
    int counter = 0;
    int msgs_limit = lua_tonumber(L, 2);

    lua_createtable(L, msgs_limit, 0);
    msg_t *msg = NULL;
    while (msgs_limit > counter) {
        msg = queue_pop(consumer->event_queues->consume_queue);
        if (msg == NULL) {
            break;
        }
        counter += 1;

        msg_t **msg_p = (msg_t **)lua_newuserdata(L, sizeof(msg));
        *msg_p = msg;

        luaL_getmetatable(L, consumer_msg_label);
        lua_setmetatable(L, -2);

        lua_rawseti(L, -2, counter);
    }
    return 1;
}

LUA_RDKAFKA_POLL_FUNC(consumer, poll_logs, LOG_QUEUE, destroy_log_msg, push_log_cb_args)
LUA_RDKAFKA_POLL_FUNC(consumer, poll_stats, STATS_QUEUE, free, push_stats_cb_args)
LUA_RDKAFKA_POLL_FUNC(consumer, poll_errors, ERROR_QUEUE, destroy_error_msg, push_errors_cb_args)

static int
lua_prepare_rebalance_callback_args_on_stack(struct lua_State *L, rebalance_msg_t *msg) {
    rd_kafka_topic_partition_t *tp = NULL;
    // creating main table
    lua_createtable(L, 0, 3); // main table
    if (msg->assigned != NULL) {
        lua_pushstring(L, "assigned"); //  "assigned" > main table
        // creating table for assigned topics
        lua_createtable(L, 0, 10);  //  table with topics > "assigned" > main table

        for (int i = 0; i < msg->assigned->cnt; i++) {
            tp = &msg->assigned->elems[i];

            lua_pushstring(L, tp->topic); //  topic name > table with topics > "assigned" > main table

            // get value from table
            lua_pushstring(L, tp->topic); //  topic name > topic name > table with topics > "assigned" > main table
            lua_gettable(L, -3); // table with partitions or nil > topic name > table with topics > "assigned" > main table

            if (lua_isnil(L, -1) == 1) {
                // removing nil from top of stack
                lua_pop(L, 1); // topic name > table with topics > "assigned" > main table
                // creating table for assigned partitions on topic
                lua_createtable(L, 0, 10); // table with partitions > topic name > table with topics > "assigned" > main table
            }

            // add partition to table
            lua_pushinteger(L, tp->partition);  // key > table with partitions > topic name > table with topics > "assigned" > main table
            luaL_pushint64(L, tp->offset);  // value > key > table with partitions > topic name > table with topics > "assigned" > main table
            lua_settable(L, -3);  // table with partitions > topic name > table with topics > "assigned" > main table

            // add table with partitions to table with topics
            lua_settable(L, -3);  // table with topics > "assigned" > main table
        }

        lua_settable(L, -3);  // main table
    }
    else if (msg->revoked != NULL) {
        lua_pushstring(L, "revoked"); //  "revoked" > main table
        // creating table for revoked topics
        lua_createtable(L, 0, 10);  //  table with topics > "revoked" > main table

        for (int i = 0; i < msg->revoked->cnt; i++) {
            tp = &msg->revoked->elems[i];

            lua_pushstring(L, tp->topic); //  topic name > table with topics > "revoked" > main table

            // get value from table
            lua_pushstring(L, tp->topic); //  topic name > topic name > table with topics > "revoked" > main table
            lua_gettable(L, -3); // table with partitions or nil > topic name > table with topics > "revoked" > main table

            if (lua_isnil(L, -1) == 1) {
                // removing nil from top of stack
                lua_pop(L, 1); // topic name > table with topics > "revoked" > main table
                // creating table for revoked partitions on topic
                lua_createtable(L, 0, 10); // table with partitions > topic name > table with topics > "revoked" > main table
            }

            // add partition to table
            lua_pushinteger(L, tp->partition);  // key > table with partitions > topic name > table with topics > "revoked" > main table
            luaL_pushint64(L, tp->offset);  // value > key > table with partitions > topic name > table with topics > "revoked" > main table
            lua_settable(L, -3);  // table with partitions > topic name > table with topics > "revoked" > main table

            // add table with partitions to table with topics
            lua_settable(L, -3);  // table with topics > "revoked" > main table
        }

        lua_settable(L, -3);  // main table
    }
    else if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        lua_pushstring(L, "error");  //  "error" > main table
        lua_pushstring(L, rd_kafka_err2str(msg->err));  // msg > "error" > main table
        lua_settable(L, -3);  // main table
    }
    else {
        return -1;
    }
    return 0;
}

int
lua_consumer_poll_rebalances(struct lua_State *L) {
    if (lua_gettop(L) != 2)
        luaL_error(L, "Usage: count, err = consumer:poll_rebalances(limit)");

    consumer_t *consumer = lua_check_consumer(L, 1);
    if (consumer->event_queues == NULL ||
        consumer->event_queues->queues[REBALANCE_QUEUE] == NULL ||
        consumer->event_queues->cb_refs[REBALANCE_QUEUE] == LUA_REFNIL) {
        lua_pushnumber(L, 0);
        lua_pushliteral(L, "Consumer poll rebalances error: callback for rebalance is not set");
        return 2;
    }

    int limit = lua_tonumber(L, 2);
    rebalance_msg_t *msg = NULL;
    int count = 0;
    char *err_str = NULL;

    while (count < limit) {
        msg = queue_pop(consumer->event_queues->queues[REBALANCE_QUEUE]);
        if (msg == NULL) {
            break;
        }
        count++;

        pthread_mutex_lock(&msg->lock);

        // push callback on stack
        lua_rawgeti(L, LUA_REGISTRYINDEX, consumer->event_queues->cb_refs[REBALANCE_QUEUE]);

        // push rebalance args on stack
        if (lua_prepare_rebalance_callback_args_on_stack(L, msg) == 0) {
            /* do the call (1 arguments, 0 result) */
            if (lua_pcall(L, 1, 0, 0) != 0) {
                err_str = (char *)lua_tostring(L, -1);
            }
        } else {
            err_str = "unknown error on rebalance callback args processing";
        }

        // allowing background thread proceed rebalancing
        pthread_cond_signal(&msg->sync);

        pthread_mutex_unlock(&msg->lock);

        if (err_str != NULL) {
            break;
        }
    }

    lua_pushnumber(L, (double)count);
    if (err_str != NULL) {
        lua_pushstring(L, err_str);
    } else {
        lua_pushnil(L);
    }
    return 2;
}

int
lua_consumer_store_offset(struct lua_State *L) {
    if (lua_gettop(L) != 2)
        luaL_error(L, "Usage: err = consumer:store_offset(msg)");

    const msg_t *msg = lua_check_consumer_msg(L, 2);
    rd_kafka_resp_err_t err = rd_kafka_offset_store(msg->topic, msg->partition, msg->offset);
    if (err) {
        lua_pushstring(L, rd_kafka_err2str(err));
        return 1;
    }
    return 0;
}

static ssize_t
wait_consumer_close(va_list args) {
    rd_kafka_t *rd_consumer = va_arg(args, rd_kafka_t *);
    rd_kafka_message_t *rd_msg = NULL;
    rd_kafka_resp_err_t err = RD_KAFKA_RESP_ERR_NO_ERROR;
    int errors_count = 0;

    // cleanup consumer queue, because at other way close hangs forever
    while (true) {
        rd_msg = rd_kafka_consumer_poll(rd_consumer, 1000);
        if (rd_msg != NULL) {
            err = rd_msg->err;
            rd_kafka_message_destroy(rd_msg);
            if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                errors_count++;
                error_callback(rd_consumer, err, rd_kafka_err2str(err), rd_kafka_opaque(rd_consumer));
                // most likely there is no connection to the broker,
                // so this cycle will go on forever without this condition
                if (errors_count == 5) {
                    break;
                }
            } else {
                errors_count = 0;
            }
        } else {
            break;
        }
    }

    err = rd_kafka_consumer_close(rd_consumer);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        error_callback(rd_consumer, err, rd_kafka_err2str(err), rd_kafka_opaque(rd_consumer));
        return -1;
    }

    return 0;
}

static ssize_t
wait_consumer_destroy(va_list args) {
    rd_kafka_t *rd_kafka = va_arg(args, rd_kafka_t *);
    // prevents hanging forever
    rd_kafka_destroy_flags(rd_kafka, RD_KAFKA_DESTROY_F_NO_CONSUMER_CLOSE);
    return 0;
}

static void
consumer_destroy(struct lua_State *L, consumer_t *consumer) {
    if (consumer->rd_consumer != NULL) {
        stop_consumer_poller(consumer->poller);
    }

    if (consumer->topics != NULL) {
        rd_kafka_topic_partition_list_destroy(consumer->topics);
        consumer->topics = NULL;
    }

    /*
     * Here we close consumer and only then destroys other stuff.
     * Otherwise raise condition is possible when e.g.
     * event queue is destroyed but consumer still receives logs, errors, etc.
     * Only topics should be destroyed.
     */
    if (consumer->rd_consumer != NULL) {
        /* Destroy handle */
        // FIXME: kafka_destroy hangs forever
        coio_call(wait_consumer_destroy, consumer->rd_consumer);
        consumer->rd_consumer = NULL;
    }

    if (consumer->poller != NULL) {
        destroy_consumer_poller(consumer->poller);
        consumer->poller = NULL;
    }

    if (consumer->event_queues != NULL) {
        destroy_event_queues(L, consumer->event_queues);
        consumer->event_queues = NULL;
    }

    free(consumer);
}

int
lua_consumer_close(struct lua_State *L) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, 1, consumer_label);
    if (consumer_p == NULL || *consumer_p == NULL) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // unsubscribe consumer to make possible close it
    rd_kafka_unsubscribe((*consumer_p)->rd_consumer);
    rd_kafka_commit((*consumer_p)->rd_consumer, NULL, 0); // sync commit of current offsets

    // trying to close in background until success
    coio_call(wait_consumer_close, (*consumer_p)->rd_consumer);
    lua_pushboolean(L, 1);
    return 1;
}

int
lua_consumer_destroy(struct lua_State *L) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, 1, consumer_label);
    if (consumer_p && *consumer_p) {
        consumer_destroy(L, *consumer_p);
    }
    if (consumer_p)
        *consumer_p = NULL;
    return 0;
}

int
lua_create_consumer(struct lua_State *L) {
    if (lua_gettop(L) != 1 || !lua_istable(L, 1))
        luaL_error(L, "Usage: consumer, err = create_consumer(conf)");

    lua_pushstring(L, "brokers");
    lua_gettable(L, -2);
    const char *brokers = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (brokers == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "consumer config table must have non nil key 'brokers' which contains string");
        return 2;
    }

    char errstr[512];
    rd_kafka_conf_t *rd_config = rd_kafka_conf_new();

    rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
    lua_pushstring(L, "default_topic_options");
    lua_gettable(L, -2);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        // stack now contains: -1 => nil; -2 => table
        while (lua_next(L, -2)) {
            // stack now contains: -1 => value; -2 => key; -3 => table
            if (!(lua_isstring(L, -1)) || !(lua_isstring(L, -2))) {
                lua_pushnil(L);
                lua_pushliteral(L, "consumer config default topic options must contains only string keys and string values");
                return 2;
            }

            const char *value = lua_tostring(L, -1);
            const char *key = lua_tostring(L, -2);
            if (rd_kafka_topic_conf_set(topic_conf, key, value, errstr, sizeof(errstr))) {
                lua_pushnil(L);
                lua_pushstring(L, errstr);
                return 2;
            }

            // pop value, leaving original key
            lua_pop(L, 1);
            // stack now contains: -1 => key; -2 => table
        }
        // stack now contains: -1 => table
    }
    lua_pop(L, 1);
    rd_kafka_conf_set_default_topic_conf(rd_config, topic_conf);

    event_queues_t *event_queues = new_event_queues();
    event_queues->consume_queue = new_queue();

    for (int i = 0; i < MAX_QUEUE; i++) {
        lua_pushstring(L, queue2str[i]);
        lua_gettable(L, -2);
        if (lua_isfunction(L, -1)) {
            event_queues->cb_refs[i] = luaL_ref(L, LUA_REGISTRYINDEX);
            event_queues->queues[i] = new_queue();
            switch (i) {
                case LOG_QUEUE:
                    rd_kafka_conf_set_log_cb(rd_config, log_callback);
                    break;
                case ERROR_QUEUE:
                    rd_kafka_conf_set_error_cb(rd_config, error_callback);
                    break;
                case STATS_QUEUE:
                    rd_kafka_conf_set_stats_cb(rd_config, stats_callback);
                    break;
                case REBALANCE_QUEUE:
                    rd_kafka_conf_set_rebalance_cb(rd_config, rebalance_callback);
                    break;
            }
        } else {
            lua_pop(L, 1);
        }
    }

    rd_kafka_conf_set_opaque(rd_config, event_queues);

    lua_pushstring(L, "options");
    lua_gettable(L, -2);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        // stack now contains: -1 => nil; -2 => table
        while (lua_next(L, -2)) {
            // stack now contains: -1 => value; -2 => key; -3 => table
            if (!(lua_isstring(L, -1)) || !(lua_isstring(L, -2))) {
                lua_pushnil(L);
                lua_pushliteral(L, "consumer config options must contains only string keys and string values");
                return 2;
            }

            const char *value = lua_tostring(L, -1);
            const char *key = lua_tostring(L, -2);
            if (rd_kafka_conf_set(rd_config, key, value, errstr, sizeof(errstr))) {
                lua_pushnil(L);
                lua_pushstring(L, errstr);
                return 2;
            }

            // pop value, leaving original key
            lua_pop(L, 1);
            // stack now contains: -1 => key; -2 => table
        }
        // stack now contains: -1 => table
    }
    lua_pop(L, 1);

    rd_kafka_t *rd_consumer;
    if (!(rd_consumer = rd_kafka_new(RD_KAFKA_CONSUMER, rd_config, errstr, sizeof(errstr)))) {
        lua_pushnil(L);
        lua_pushstring(L, errstr);
        return 2;
    }

    if (rd_kafka_brokers_add(rd_consumer, brokers) == 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "No valid brokers specified");
        return 2;
    }

    rd_kafka_poll_set_consumer(rd_consumer);

    // creating background thread for polling consumer
    consumer_poller_t *poller = new_consumer_poller(rd_consumer);

    consumer_t *consumer;
    consumer = malloc(sizeof(consumer_t));
    consumer->rd_consumer = rd_consumer;
    consumer->topics = NULL;
    consumer->event_queues = event_queues;
    consumer->poller = poller;

    consumer_t **consumer_p = (consumer_t **)lua_newuserdata(L, sizeof(consumer));
    *consumer_p = consumer;

    luaL_getmetatable(L, consumer_label);
    lua_setmetatable(L, -2);
    return 1;
}

int
lua_consumer_dump_conf(struct lua_State *L) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, 1, consumer_label);
    if (consumer_p == NULL || *consumer_p == NULL)
        return 0;

    if ((*consumer_p)->rd_consumer != NULL)
        return lua_librdkafka_dump_conf(L, (*consumer_p)->rd_consumer);
    return 0;
}

int
lua_consumer_metadata(struct lua_State *L) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, 1, consumer_label);
    if (consumer_p == NULL || *consumer_p == NULL)
        return 0;

    if ((*consumer_p)->rd_consumer != NULL) {
        int timeout_ms = lua_tointeger(L, 2);
        return lua_librdkafka_metadata(L, (*consumer_p)->rd_consumer, NULL, timeout_ms);
    }
    return 0;
}

int
lua_consumer_list_groups(struct lua_State *L) {
    consumer_t **consumer_p = (consumer_t **)luaL_checkudata(L, 1, consumer_label);
    if (consumer_p == NULL || *consumer_p == NULL)
        return 0;

    if ((*consumer_p)->rd_consumer != NULL) {
        const char *group = lua_tostring(L, 2);
        int timeout_ms = lua_tointeger(L, 3);
        return lua_librdkafka_list_groups(L, (*consumer_p)->rd_consumer, group, timeout_ms);
    }
    return 0;
}
