#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

#include <common.h>
#include <queue.h>
#include <callbacks.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Common callbacks handling
 */

/**
 * Handle logs from RDKafka
 */

log_msg_t *
new_log_msg(int level, const char *fac, const char *buf) {
    log_msg_t *msg = malloc(sizeof(log_msg_t));
    if (msg == NULL) {
        return NULL;
    }
    msg->level = level;
    msg->fac = malloc(sizeof(char) * strlen(fac) + 1);
    strcpy(msg->fac, fac);
    msg->buf = malloc(sizeof(char) * strlen(buf) + 1);
    strcpy(msg->buf, buf);
    return msg;
}

void
destroy_log_msg(log_msg_t *msg) {
    if (msg->fac != NULL)
        free(msg->fac);
    if (msg->buf != NULL)
        free(msg->buf);
    free(msg);
}

void
log_callback(const rd_kafka_t *rd_kafka, int level, const char *fac, const char *buf) {
    event_queues_t *event_queues = rd_kafka_opaque(rd_kafka);
    if (event_queues != NULL && event_queues->queues[LOG_QUEUE] != NULL) {
        log_msg_t *msg = new_log_msg(level, fac, buf);
        if (msg != NULL && queue_push(event_queues->queues[LOG_QUEUE], msg) != 0) {
            destroy_log_msg(msg);
        }
    }
}

int
stats_callback(rd_kafka_t *rd_kafka, char *json, size_t json_len, void *opaque) {
    (void)opaque;
    (void)json_len;
    event_queues_t *event_queues = rd_kafka_opaque(rd_kafka);
    if (event_queues != NULL && event_queues->queues[STATS_QUEUE] != NULL) {
        if (json != NULL) {
            if (queue_push(event_queues->queues[STATS_QUEUE], json) != 0)
                return 0; // destroy json after return
            return 1; // json should be freed manually
        }
    }
    return 0;
}

/**
 * Handle errors from RDKafka
 */

error_msg_t *
new_error_msg(int err, const char *reason) {
    error_msg_t *msg = malloc(sizeof(error_msg_t));
    if (msg == NULL)
        return NULL;
    msg->err = err;
    msg->reason = malloc(sizeof(char) * strlen(reason) + 1);
    strcpy(msg->reason, reason);
    return msg;
}

void
destroy_error_msg(error_msg_t *msg) {
    if (msg->reason != NULL)
        free(msg->reason);
    free(msg);
}

void
error_callback(rd_kafka_t *UNUSED(rd_kafka), int err, const char *reason, void *opaque) {
    event_queues_t *event_queues = opaque;
    if (event_queues != NULL && event_queues->queues[ERROR_QUEUE] != NULL) {
        error_msg_t *msg = new_error_msg(err, reason);
        if (msg != NULL && queue_push(event_queues->queues[ERROR_QUEUE], msg) != 0)
            destroy_error_msg(msg);
    }
}

int
push_log_cb_args(struct lua_State *L, const log_msg_t *msg)
{
    lua_pushstring(L, msg->fac);
    lua_pushstring(L, msg->buf);
    lua_pushinteger(L, msg->level);
    return 3;
}

int
push_stats_cb_args(struct lua_State *L, const char *msg)
{
    lua_pushstring(L, msg);
    return 1;
}

int
push_errors_cb_args(struct lua_State *L, const error_msg_t *msg)
{
    lua_pushstring(L, msg->reason);
    return 1;
}

/**
 * Handle message delivery reports from RDKafka
 */

dr_msg_t *
new_dr_msg(int dr_callback, int err) {
    dr_msg_t *dr_msg;
    dr_msg = malloc(sizeof(dr_msg_t));
    dr_msg->dr_callback = dr_callback;
    dr_msg->err = err;
    return dr_msg;
}

void
destroy_dr_msg(dr_msg_t *dr_msg) {
    free(dr_msg);
}

void
msg_delivery_callback(rd_kafka_t *UNUSED(producer), const rd_kafka_message_t *msg, void *opaque) {
    event_queues_t *event_queues = opaque;
    if (msg->_private != NULL && event_queues != NULL && event_queues->delivery_queue != NULL) {
        dr_msg_t *dr_msg = msg->_private;
        if (dr_msg != NULL) {
            if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                dr_msg->err = msg->err;
            }
            queue_push(event_queues->delivery_queue, dr_msg);
        }
    }
}

/**
 * Handle rebalance callbacks from RDKafka
 */

rebalance_msg_t *
new_rebalance_revoke_msg(rd_kafka_topic_partition_list_t *revoked) {
    rebalance_msg_t *msg = malloc(sizeof(rebalance_msg_t));
    if (msg == NULL) {
        return NULL;
    }

    pthread_mutex_t lock;
    if (pthread_mutex_init(&lock, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->lock = lock;

    pthread_cond_t sync;
    if (pthread_cond_init(&sync, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->sync = sync;
    msg->revoked = revoked;
    msg->assigned = NULL;
    msg->err = RD_KAFKA_RESP_ERR_NO_ERROR;
    return msg;
}

rebalance_msg_t *
new_rebalance_assign_msg(rd_kafka_topic_partition_list_t *assigned) {
    rebalance_msg_t *msg = malloc(sizeof(rebalance_msg_t));
    if (msg == NULL) {
        return NULL;
    }

    pthread_mutex_t lock;
    if (pthread_mutex_init(&lock, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->lock = lock;

    pthread_cond_t sync;
    if (pthread_cond_init(&sync, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->sync = sync;
    msg->revoked = NULL;
    msg->assigned = assigned;
    msg->err = RD_KAFKA_RESP_ERR_NO_ERROR;
    return msg;
}

rebalance_msg_t *
new_rebalance_error_msg(rd_kafka_resp_err_t err) {
    rebalance_msg_t *msg = malloc(sizeof(rebalance_msg_t));
    if (msg == NULL) {
        return NULL;
    }

    pthread_mutex_t lock;
    if (pthread_mutex_init(&lock, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->lock = lock;

    pthread_cond_t sync;
    if (pthread_cond_init(&sync, NULL) != 0) {
        free(msg);
        return NULL;
    }

    msg->sync = sync;
    msg->revoked = NULL;
    msg->assigned = NULL;
    msg->err = err;
    return msg;
}

void
destroy_rebalance_msg(rebalance_msg_t *rebalance_msg) {
    pthread_mutex_destroy(&rebalance_msg->lock);
    pthread_cond_destroy(&rebalance_msg->sync);
    free(rebalance_msg);
}

void
rebalance_callback(rd_kafka_t *consumer, rd_kafka_resp_err_t err, rd_kafka_topic_partition_list_t *partitions, void *opaque) {
    event_queues_t *event_queues = opaque;
    rebalance_msg_t *msg = NULL;
    switch (err)
    {
        case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
            msg = new_rebalance_assign_msg(partitions);
            if (msg != NULL) {

                pthread_mutex_lock(&msg->lock);

                if (queue_push(event_queues->queues[REBALANCE_QUEUE], msg) == 0) {
                    // waiting while main TX thread invokes rebalance callback
                    pthread_cond_wait(&msg->sync, &msg->lock);
                }

                pthread_mutex_unlock(&msg->lock);

                destroy_rebalance_msg(msg);
            }
            rd_kafka_assign(consumer, partitions);
            break;

        case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
            rd_kafka_commit(consumer, partitions, 0); // sync commit

            msg = new_rebalance_revoke_msg(partitions);
            if (msg != NULL) {

                pthread_mutex_lock(&msg->lock);

                if (queue_push(event_queues->queues[REBALANCE_QUEUE], msg) == 0) {
                    // waiting while main TX thread invokes rebalance callback
                    pthread_cond_wait(&msg->sync, &msg->lock);
                }

                pthread_mutex_unlock(&msg->lock);

                destroy_rebalance_msg(msg);
            }

            rd_kafka_assign(consumer, NULL);
            break;

        default:
            msg = new_rebalance_error_msg(err);
            if (msg != NULL) {

                pthread_mutex_lock(&msg->lock);

                if (queue_push(event_queues->queues[REBALANCE_QUEUE], msg) == 0) {
                    // waiting while main TX thread invokes rebalance callback
                    pthread_cond_wait(&msg->sync, &msg->lock);
                }

                pthread_mutex_unlock(&msg->lock);

                destroy_rebalance_msg(msg);
            }
            rd_kafka_assign(consumer, NULL);
            break;
    }
}

/**
 * Structure which contains all queues for communication between main TX thread and
 * RDKafka callbacks from background threads
 */

event_queues_t *
new_event_queues() {
    event_queues_t *event_queues = calloc(1, sizeof(event_queues_t));
    for (int i = 0; i < MAX_QUEUE; i++)
        event_queues->cb_refs[i] = LUA_REFNIL;
    return event_queues;
}

void
destroy_event_queues(struct lua_State *L, event_queues_t *event_queues) {
    if (event_queues->consume_queue != NULL) {
        rd_kafka_message_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->consume_queue);
            if (msg == NULL)
                break;
            rd_kafka_message_destroy(msg);
        }
        destroy_queue(event_queues->consume_queue);
    }
    if (event_queues->delivery_queue != NULL) {
        dr_msg_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->delivery_queue);
            if (msg == NULL)
                break;
            luaL_unref(L, LUA_REGISTRYINDEX, msg->dr_callback);
            destroy_dr_msg(msg);
        }
        destroy_queue(event_queues->delivery_queue);
    }

    for (int i = 0; i < MAX_QUEUE; i++) {
        if (event_queues->queues[i] == NULL)
            continue;
        while (true) {
            void *msg = queue_pop(event_queues->queues[i]);
            if (msg == NULL)
                break;

            switch (i) {
                case LOG_QUEUE:
                    destroy_log_msg(msg);
                    break;
                case STATS_QUEUE:
                    break;
                case ERROR_QUEUE:
                    destroy_error_msg(msg);
                    break;
                case REBALANCE_QUEUE: {
                    rebalance_msg_t *rebalance_msg = msg;
                    pthread_mutex_lock(&rebalance_msg->lock);
                    // allowing background thread proceed rebalancing
                    pthread_cond_signal(&rebalance_msg->sync);
                    pthread_mutex_unlock(&rebalance_msg->lock);
                    break;
                }
            }
        }
        destroy_queue(event_queues->queues[i]);
    }

    for (int i = 0; i < MAX_QUEUE; i++)
        luaL_unref(L, LUA_REGISTRYINDEX, event_queues->cb_refs[i]);

    free(event_queues);
}
