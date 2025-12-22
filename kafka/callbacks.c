#include "callbacks.h"
#include "common.h"
#include "consumer_msg.h"
#include "queue.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Common callbacks handling
 */

/**
 * Handle logs from RDKafka
 */

log_msg_t *
new_log_msg(int level, const char *fac, const char *buf) {
    log_msg_t *msg = xmalloc(sizeof(log_msg_t));
    msg->level = level;
    msg->fac = xmalloc(sizeof(char) * strlen(fac) + 1);
    strcpy(msg->fac, fac);
    msg->buf = xmalloc(sizeof(char) * strlen(buf) + 1);
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
    error_msg_t *msg = xmalloc(sizeof(error_msg_t));
    msg->err = err;
    msg->reason = xmalloc(sizeof(char) * strlen(reason) + 1);
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
error_callback(rd_kafka_t *rd_kafka, int err, const char *reason, void *opaque) {
    (void)rd_kafka;
    event_queues_t *event_queues = opaque;
    if (event_queues != NULL && event_queues->queues[ERROR_QUEUE] != NULL) {
        error_msg_t *msg = new_error_msg(err, reason);
        if (msg != NULL && queue_push(event_queues->queues[ERROR_QUEUE], msg) != 0)
            destroy_error_msg(msg);
    }
}

int
push_log_cb_args(struct lua_State *L, const log_msg_t *msg) {
    lua_pushstring(L, msg->fac);
    lua_pushstring(L, msg->buf);
    lua_pushinteger(L, msg->level);
    return 3;
}

int
push_stats_cb_args(struct lua_State *L, const char *msg) {
    lua_pushstring(L, msg);
    return 1;
}

int
push_errors_cb_args(struct lua_State *L, const error_msg_t *msg) {
    lua_pushstring(L, msg->reason);
    return 1;
}

/**
 * Handle message delivery reports from RDKafka
 */

dr_msg_t *
new_dr_msg(int dr_callback, int err) {
    dr_msg_t *dr_msg;
    dr_msg = xmalloc(sizeof(dr_msg_t));
    dr_msg->dr_callback = dr_callback;
    dr_msg->err = err;
    return dr_msg;
}

void
destroy_dr_msg(dr_msg_t *dr_msg) {
    free(dr_msg);
}

void
msg_delivery_callback(rd_kafka_t *producer, const rd_kafka_message_t *msg, void *opaque) {
    (void)producer;
    event_queues_t *event_queues = opaque;
    if (msg->_private == NULL || event_queues == NULL || event_queues->delivery_queue == NULL)
        return;

    dr_msg_t *dr_msg = msg->_private;
    if (dr_msg != NULL) {
        if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            dr_msg->err = msg->err;
        }
        queue_push(event_queues->delivery_queue, dr_msg);
    }
}

/**
 * Handle rebalance callbacks from RDKafka
 */

rebalance_msg_t *
new_rebalance_msg(rebalance_event_kind_t kind,
                  const rd_kafka_topic_partition_list_t *partitions,
                  rd_kafka_resp_err_t err) {
    rebalance_msg_t *msg = xcalloc(1, sizeof(*msg));
    msg->kind = kind;
    msg->err = err;

    if (partitions != NULL) {
        msg->partitions = rd_kafka_topic_partition_list_copy(partitions);
    }
    return msg;
}

void
destroy_rebalance_msg(rebalance_msg_t *msg) {
    if (msg == NULL)
        return;
    if (msg->partitions != NULL)
        rd_kafka_topic_partition_list_destroy(msg->partitions);
    free(msg);
}

static void
push_rebalance_event_if_needed(event_queues_t *eq,
                               rebalance_event_kind_t kind,
                               const rd_kafka_topic_partition_list_t *partitions,
                               rd_kafka_resp_err_t err) {
    if (eq == NULL)
        return;
    if (eq->queues[REBALANCE_QUEUE] == NULL)
        return;
    if (eq->cb_refs[REBALANCE_QUEUE] == LUA_REFNIL)
        return;

    rebalance_msg_t *msg = new_rebalance_msg(kind, partitions, err);
    if (msg == NULL)
        return;

    if (queue_push(eq->queues[REBALANCE_QUEUE], msg) != 0) {
        destroy_rebalance_msg(msg);
    }
}

void
rebalance_callback(rd_kafka_t *consumer,
                   rd_kafka_resp_err_t err,
                   rd_kafka_topic_partition_list_t *partitions,
                   void *opaque)
{
    event_queues_t *eq = opaque;
    const char *proto = rd_kafka_rebalance_protocol(consumer);
    int cooperative = (proto != NULL) && strcmp(proto, "COOPERATIVE") == 0;

    switch (err) {
    case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
        push_rebalance_event_if_needed(eq, REB_EVENT_ASSIGN, partitions, RD_KAFKA_RESP_ERR_NO_ERROR);
        if (cooperative)
            rd_kafka_incremental_assign(consumer, partitions);
        else
            rd_kafka_assign(consumer, partitions);
        break;

    case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
        rd_kafka_commit(consumer, partitions, 0);
        push_rebalance_event_if_needed(eq, REB_EVENT_REVOKE, partitions, RD_KAFKA_RESP_ERR_NO_ERROR);
        if (cooperative)
            rd_kafka_incremental_unassign(consumer, partitions);
        else
            rd_kafka_assign(consumer, NULL);
        break;

    default:
        push_rebalance_event_if_needed(eq, REB_EVENT_ERROR, NULL, err);
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
    event_queues_t *event_queues = xcalloc(1, sizeof(event_queues_t));
    for (int i = 0; i < MAX_QUEUE; i++)
        event_queues->cb_refs[i] = LUA_REFNIL;
    return event_queues;
}

void
destroy_event_queues(struct lua_State *L, event_queues_t *event_queues) {
    if (event_queues == NULL)
        return;
    if (event_queues->consume_queue != NULL) {
        msg_t *msg = NULL;
        while (true) {
            msg = queue_pop(event_queues->consume_queue);
            if (msg == NULL)
                break;
            destroy_consumer_msg(msg);
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
                    free(msg);
                    break;
                case ERROR_QUEUE:
                    destroy_error_msg(msg);
                    break;
                case REBALANCE_QUEUE: {
                    destroy_rebalance_msg(msg);
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
