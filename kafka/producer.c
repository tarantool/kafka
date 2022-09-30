#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <librdkafka/rdkafka.h>
#include <tarantool/module.h>

#include <common.h>
#include <callbacks.h>
#include <queue.h>

#include "producer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Producer poll thread
 */

static void *
producer_poll_loop(void *arg) {
    set_thread_name("kafka_producer");

    producer_poller_t *poller = arg;
    int count = 0;
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
            count = rd_kafka_poll(poller->rd_producer, 1000);
            if (count == 0) {
                // throttling calls with 100ms sleep
                usleep(100000);
            }
        }
    }

    pthread_exit(NULL);
}

static producer_poller_t *
new_producer_poller(rd_kafka_t *rd_producer) {
    producer_poller_t *poller = malloc(sizeof(producer_poller_t));
    if (poller == NULL)
        return NULL;

    poller->rd_producer = rd_producer;
    poller->should_stop = 0;

    pthread_mutex_init(&poller->lock, NULL);
    pthread_attr_init(&poller->attr);
    pthread_attr_setdetachstate(&poller->attr, PTHREAD_CREATE_JOINABLE);
    int rc = pthread_create(&poller->thread, &poller->attr, producer_poll_loop, (void *)poller);
    if (rc < 0) {
        free(poller);
        return NULL;
    }

    return poller;
}

static ssize_t
stop_poller(va_list args) {
    producer_poller_t *poller = va_arg(args, producer_poller_t *);
    pthread_mutex_lock(&poller->lock);

    poller->should_stop = 1;

    pthread_mutex_unlock(&poller->lock);

    pthread_join(poller->thread, NULL);

    return 0;
}

static void
destroy_producer_poller(producer_poller_t *poller) {
    // stopping polling thread
    coio_call(stop_poller, poller);

    pthread_attr_destroy(&poller->attr);
    pthread_mutex_destroy(&poller->lock);

    free(poller);
}

/**
 * Producer
 */

producer_topics_t *
new_producer_topics(int32_t capacity) {
    rd_kafka_topic_t **elements;
    elements = malloc(sizeof(rd_kafka_topic_t *) * capacity);
    if (elements == NULL)
        return NULL;

    producer_topics_t *topics;
    topics = malloc(sizeof(producer_topics_t));
    topics->capacity = capacity;
    topics->count = 0;
    topics->elements = elements;

    return topics;
}

int
add_producer_topics(producer_topics_t *topics, rd_kafka_topic_t *element) {
    if (topics->count >= topics->capacity) {
        rd_kafka_topic_t **new_elements = realloc(topics->elements, sizeof(rd_kafka_topic_t *) * topics->capacity * 2);
        if (new_elements == NULL) {
            printf("realloc failed to relloc rd_kafka_topic_t array.");
            return 1;
        }
        topics->elements = new_elements;
        topics->capacity *= 2;
    }
    topics->elements[topics->count++] = element;
    return 0;
}

static rd_kafka_topic_t *
find_producer_topic_by_name(producer_topics_t *topics, const char *name) {
    rd_kafka_topic_t *topic;
    for (int i = 0; i < topics->count; i++) {
        topic = topics->elements[i];
        if (strcmp(rd_kafka_topic_name(topic), name) == 0) {
            return topic;
        }
    }
    return NULL;
}

void
destroy_producer_topics(producer_topics_t *topics) {
    rd_kafka_topic_t **topic_p;
    rd_kafka_topic_t **end = topics->elements + topics->count;
    for (topic_p = topics->elements; topic_p < end; topic_p++) {
        rd_kafka_topic_destroy(*topic_p);
    }

    free(topics->elements);
    free(topics);
}

static inline producer_t *
lua_check_producer(struct lua_State *L, int index) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, index, producer_label);
    if (producer_p == NULL || *producer_p == NULL)
        luaL_error(L, "Kafka producer fatal error: failed to retrieve producer from lua stack!");
    return *producer_p;
}

int
lua_producer_tostring(struct lua_State *L) {
    const producer_t *producer = lua_check_producer(L, 1);
    lua_pushfstring(L, "Kafka Producer: %p", producer);
    return 1;
}

int
lua_producer_msg_delivery_poll(struct lua_State *L) {
    if (lua_gettop(L) != 2)
        luaL_error(L, "Usage: count, err = producer:msg_delivery_poll(events_limit)");

    producer_t *producer = lua_check_producer(L, 1);

    int events_limit = lua_tonumber(L, 2);
    int callbacks_count = 0;
    char *err_str = NULL;
    dr_msg_t *dr_msg = NULL;

    pthread_mutex_lock(&producer->event_queues->delivery_queue->lock);

    while (events_limit > callbacks_count) {
        dr_msg = queue_lockfree_pop(producer->event_queues->delivery_queue);
        if (dr_msg == NULL)
            break;
        callbacks_count += 1;
        lua_rawgeti(L, LUA_REGISTRYINDEX, dr_msg->dr_callback);
        if (dr_msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            lua_pushstring(L, rd_kafka_err2str(dr_msg->err));
        } else {
            lua_pushnil(L);
        }
        /* do the call (1 arguments, 0 result) */
        if (lua_pcall(L, 1, 0, 0) != 0) {
            err_str = (char *)lua_tostring(L, -1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, dr_msg->dr_callback);
        destroy_dr_msg(dr_msg);
        if (err_str != NULL) {
            break;
        }
    }

    pthread_mutex_unlock(&producer->event_queues->delivery_queue->lock);

    lua_pushnumber(L, (double)callbacks_count);
    if (err_str != NULL) {
        lua_pushstring(L, err_str);
    } else {
        lua_pushnil(L);
    }
    return 2;
}

LUA_RDKAFKA_POLL_FUNC(producer, poll_logs, LOG_QUEUE, destroy_log_msg, push_log_cb_args)
LUA_RDKAFKA_POLL_FUNC(producer, poll_stats, STATS_QUEUE, free, push_stats_cb_args)
LUA_RDKAFKA_POLL_FUNC(producer, poll_errors, ERROR_QUEUE, destroy_error_msg, push_errors_cb_args)

int
lua_producer_produce(struct lua_State *L) {
    if (lua_gettop(L) != 2 || !lua_istable(L, 2))
        luaL_error(L, "Usage: err = producer:produce(msg)");

    lua_pushliteral(L, "topic");
    lua_gettable(L, -2);
    const char *topic = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (topic == NULL) {
        lua_pushliteral(L, "producer message must contains non nil 'topic' key");
        return 1;
    }

    lua_pushliteral(L, "key");
    lua_gettable(L, -2);
    size_t key_len;
    // rd_kafka will copy key so no need to worry about this cast
    char *key = (char *)lua_tolstring(L, -1, &key_len);

    lua_pop(L, 1);

    lua_pushliteral(L, "value");
    lua_gettable(L, -2);
    size_t value_len;
    // rd_kafka will copy value so no need to worry about this cast
    char *value = (char *)lua_tolstring(L, -1, &value_len);

    lua_pop(L, 1);

    if (key == NULL && value == NULL) {
        lua_pushliteral(L, "producer message must contains non nil key or value");
        return 1;
    }

    rd_kafka_headers_t *hdrs = NULL;
    lua_pushliteral(L, "headers");
    lua_gettable(L, -2);
    if (lua_istable(L, -1)) {
        hdrs = rd_kafka_headers_new(8);
        if (hdrs == NULL) {
            lua_pushliteral(L, "failed to allocate kafka headers");
            return 1;
        }

        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            size_t hdr_value_len = 0;
            const char *hdr_value = lua_tolstring(L, -1, &hdr_value_len);
            size_t hdr_key_len = 0;
            const char *hdr_key = lua_tolstring(L, -2, &hdr_key_len);

            rd_kafka_resp_err_t err = rd_kafka_header_add(
                    hdrs, hdr_key, hdr_key_len, hdr_value, hdr_value_len);
            if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                lua_pushliteral(L, "failed to add kafka headers");
                goto error;
            }

            lua_pop(L, 1);
        }
    }

    lua_pop(L, 1);

    // create delivery callback queue if got msg id
    dr_msg_t *dr_msg = NULL;
    lua_pushliteral(L, "dr_callback");
    lua_gettable(L, -2);
    if (lua_isfunction(L, -1)) {
        dr_msg = new_dr_msg(luaL_ref(L, LUA_REGISTRYINDEX), RD_KAFKA_RESP_ERR_NO_ERROR);
        if (dr_msg == NULL) {
            lua_pushliteral(L, "failed to create callback message");
            goto error;
        }
    } else {
        lua_pop(L, 1);
    }

    // pop msg
    lua_pop(L, 1);

    producer_t *producer = lua_check_producer(L, 1);
    rd_kafka_topic_t *rd_topic = find_producer_topic_by_name(producer->topics, topic);
    if (rd_topic == NULL) {
        rd_topic = rd_kafka_topic_new(producer->rd_producer, topic, NULL);
        if (rd_topic == NULL) {
            lua_pushstring(L, rd_kafka_err2str(rd_kafka_last_error()));
            goto error;
        }
        if (add_producer_topics(producer->topics, rd_topic) != 0) {
            lua_pushstring(L, "Unexpected error: failed to add new topic to topic list!");
            goto error;
        }
    }

    rd_kafka_resp_err_t err = RD_KAFKA_RESP_ERR_NO_ERROR;
    if (hdrs == NULL) {
        int rc = rd_kafka_produce(rd_topic, -1, RD_KAFKA_MSG_F_COPY, value, value_len, key, key_len, dr_msg);
        if (rc != 0)
            err = rd_kafka_last_error();
    } else {
        err = rd_kafka_producev(
                producer->rd_producer,
                RD_KAFKA_V_RKT(rd_topic),
                RD_KAFKA_V_PARTITION(-1),
                RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                RD_KAFKA_V_VALUE(value, value_len),
                RD_KAFKA_V_KEY(key, key_len),
                RD_KAFKA_V_HEADERS(hdrs),
                RD_KAFKA_V_OPAQUE(dr_msg),
                RD_KAFKA_V_END);
        if (err != RD_KAFKA_RESP_ERR_NO_ERROR)
            rd_kafka_headers_destroy(hdrs);
    }

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        lua_pushstring(L, rd_kafka_err2str(err));
        return 1;
    }
    return 0;

error:
    if (hdrs != NULL)
        rd_kafka_headers_destroy(hdrs);
    return 1;
}

static ssize_t
producer_flush(va_list args) {
    rd_kafka_resp_err_t err = RD_KAFKA_RESP_ERR_NO_ERROR;
    rd_kafka_t *rd_producer = va_arg(args, rd_kafka_t *);
    while (true) {
        err = rd_kafka_flush(rd_producer, 1000);
        if (err != RD_KAFKA_RESP_ERR__TIMED_OUT) {
            break;
        }
    }
    return 0;
}

static ssize_t
wait_producer_destroy(va_list args) {
    rd_kafka_t *rd_kafka = va_arg(args, rd_kafka_t *);
    rd_kafka_destroy(rd_kafka);
    return 0;
}

static void
destroy_producer(struct lua_State *L, producer_t *producer) {
    if (producer->topics != NULL) {
        destroy_producer_topics(producer->topics);
        producer->topics = NULL;
    }

    /*
     * Here we close producer and only then destroys other stuff.
     * Otherwise raise condition is possible when e.g.
     * event queue is destroyed but producer still receives logs, errors, etc.
     * Only topics should be destroyed.
     */
    if (producer->rd_producer != NULL) {
        /* Destroy handle */
        coio_call(wait_producer_destroy, producer->rd_producer);
        producer->rd_producer = NULL;
    }

    if (producer->poller != NULL) {
        destroy_producer_poller(producer->poller);
        producer->poller = NULL;
    }

    if (producer->event_queues != NULL) {
        destroy_event_queues(L, producer->event_queues);
        producer->event_queues = NULL;
    }

    free(producer);
}

int
lua_producer_close(struct lua_State *L) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, 1, producer_label);
    if (producer_p == NULL || *producer_p == NULL) {
        lua_pushboolean(L, 0);
        return 1;
    }

    if ((*producer_p)->rd_producer != NULL) {
        coio_call(producer_flush, (*producer_p)->rd_producer);
    }

    if ((*producer_p)->poller != NULL) {
        destroy_producer_poller((*producer_p)->poller);
        (*producer_p)->poller = NULL;
    }

    lua_pushboolean(L, 1);
    return 1;
}

int
lua_producer_dump_conf(struct lua_State *L) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, 1, producer_label);
    if (producer_p == NULL || *producer_p == NULL)
        return 0;

    if ((*producer_p)->rd_producer != NULL)
        return lua_librdkafka_dump_conf(L, (*producer_p)->rd_producer);
    return 0;
}

int
lua_producer_destroy(struct lua_State *L) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, 1, producer_label);
    if (producer_p && *producer_p) {
        destroy_producer(L, *producer_p);
    }
    if (producer_p)
        *producer_p = NULL;
    return 0;
}

int
lua_create_producer(struct lua_State *L) {
    if (lua_gettop(L) != 1 || !lua_istable(L, 1))
        luaL_error(L, "Usage: producer, err = create_producer(conf)");

    lua_pushstring(L, "brokers");
    lua_gettable(L, -2);
    const char *brokers = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (brokers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "producer config table must have non nil key 'brokers' which contains string");
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
                lua_pushliteral(L, "producer config default topic options must contains only string keys and string values");
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
    event_queues->delivery_queue = new_queue();
    rd_kafka_conf_set_dr_msg_cb(rd_config, msg_delivery_callback);

    for (int i = 0; i < MAX_QUEUE; i++) {
        if (i == REBALANCE_QUEUE)
            continue;

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
                lua_pushliteral(L, "producer config options must contains only string keys and string values");
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

    rd_kafka_t *rd_producer;
    if (!(rd_producer = rd_kafka_new(RD_KAFKA_PRODUCER, rd_config, errstr, sizeof(errstr)))) {
        lua_pushnil(L);
        lua_pushstring(L, errstr);
        return 2;
    }

    if (rd_kafka_brokers_add(rd_producer, brokers) == 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "No valid brokers specified");
        return 2;
    }

    // creating background thread for polling consumer
    producer_poller_t *poller = new_producer_poller(rd_producer);

    producer_t *producer;
    producer = malloc(sizeof(producer_t));
    producer->rd_producer = rd_producer;
    producer->topics = new_producer_topics(256);
    producer->event_queues = event_queues;
    producer->poller = poller;

    producer_t **producer_p = (producer_t **)lua_newuserdata(L, sizeof(producer));
    *producer_p = producer;

    luaL_getmetatable(L, producer_label);
    lua_setmetatable(L, -2);
    return 1;
}

int
lua_producer_metadata(struct lua_State *L) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, 1, producer_label);
    if (producer_p == NULL || *producer_p == NULL)
        return 0;

    if ((*producer_p)->rd_producer != NULL) {
        rd_kafka_topic_t *topic = NULL;
        const char *topic_name = lua_tostring(L, 2);
        if (topic_name != NULL) {
            topic = find_producer_topic_by_name((*producer_p)->topics, topic_name);
            if (topic == NULL) {
                lua_pushnil(L);
                lua_pushfstring(L, "Topic \"%s\" is not found", topic_name);
                return 2;
            }
        }

        int timeout_ms = lua_tointeger(L, 3);
        return lua_librdkafka_metadata(L, (*producer_p)->rd_producer, topic, timeout_ms);
    }
    return 0;
}

int
lua_producer_list_groups(struct lua_State *L) {
    producer_t **producer_p = (producer_t **)luaL_checkudata(L, 1, producer_label);
    if (producer_p == NULL || *producer_p == NULL)
        return 0;

    if ((*producer_p)->rd_producer != NULL) {
        const char *group = lua_tostring(L, 2);
        int timeout_ms = lua_tointeger(L, 3);
        return lua_librdkafka_list_groups(L, (*producer_p)->rd_producer, group, timeout_ms);
    }
    return 0;
}
