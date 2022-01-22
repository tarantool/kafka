#ifndef TNT_KAFKA_CALLBACKS_H
#define TNT_KAFKA_CALLBACKS_H

#include <pthread.h>
#include <stddef.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

#include <queue.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Common callbacks handling
 */

/**
 * Handle logs from RDKafka
 */

typedef struct {
    int level;
    char *fac;
    char *buf;
} log_msg_t;

log_msg_t *
new_log_msg(int level, const char *fac, const char *buf);

void
destroy_log_msg(log_msg_t *msg);

void
log_callback(const rd_kafka_t *rd_kafka, int level, const char *fac, const char *buf);

int
push_log_cb_args(struct lua_State *L, const log_msg_t *msg);

/**
 * Handle stats from RDKafka
 */

int
stats_callback(rd_kafka_t *rd_kafka, char *json, size_t json_len, void *opaque);

int
push_stats_cb_args(struct lua_State *L, const char *msg);

/**
 * Handle errors from RDKafka
 */

typedef struct {
    int err;
    char *reason;
} error_msg_t;

error_msg_t *
new_error_msg(int err, const char *reason);

void
destroy_error_msg(error_msg_t *msg);

void
error_callback(rd_kafka_t *UNUSED(rd_kafka), int err, const char *reason, void *opaque);

int
push_errors_cb_args(struct lua_State *L, const error_msg_t *msg);

/**
 * Handle message delivery reports from RDKafka
 */

typedef struct {
    int dr_callback;
    int err;
} dr_msg_t;

dr_msg_t *
new_dr_msg(int dr_callback, int err);

void
destroy_dr_msg(dr_msg_t *dr_msg);

void
msg_delivery_callback(rd_kafka_t *UNUSED(producer), const rd_kafka_message_t *msg, void *opaque);


/**
 * Handle rebalance callbacks from RDKafka
 */

typedef struct {
    pthread_mutex_t                  lock;
    pthread_cond_t                   sync;
    rd_kafka_topic_partition_list_t *revoked;
    rd_kafka_topic_partition_list_t *assigned;
    rd_kafka_resp_err_t              err;
} rebalance_msg_t;

rebalance_msg_t *new_rebalance_revoke_msg(rd_kafka_topic_partition_list_t *revoked);

rebalance_msg_t *new_rebalance_assign_msg(rd_kafka_topic_partition_list_t *assigned);

rebalance_msg_t *new_rebalance_error_msg(rd_kafka_resp_err_t err);

void destroy_rebalance_msg(rebalance_msg_t *rebalance_msg);

void rebalance_callback(rd_kafka_t *consumer, rd_kafka_resp_err_t err, rd_kafka_topic_partition_list_t *partitions, void *opaque);

/**
 * Structure which contains all queues for communication between main TX thread and
 * RDKafka callbacks from background threads
 */

enum {
    LOG_QUEUE,
    STATS_QUEUE,
    ERROR_QUEUE,
    REBALANCE_QUEUE,
    MAX_QUEUE,
};

RD_UNUSED
static const char *const queue2str[] = {
        [LOG_QUEUE] = "log_callback",
        [STATS_QUEUE] = "stats_callback",
        [ERROR_QUEUE] = "error_callback",
        [REBALANCE_QUEUE] = "rebalance_callback",
};

#define LUA_RDKAFKA_POLL_FUNC(rd_type, name, queue_no, destroy_fn, push_args_fn)         \
int                                                                                      \
lua_##rd_type##_##name(struct lua_State *L) {                                            \
    if (lua_gettop(L) != 2)                                                              \
        return luaL_error(L, "Usage: count, err = " #rd_type ":" #name "(limit)");           \
                                                                                         \
    rd_type##_t *rd = lua_check_##rd_type(L, 1);                                         \
    if (rd->event_queues == NULL ||                                                      \
        rd->event_queues->queues[queue_no] == NULL ||                                    \
        rd->event_queues->cb_refs[queue_no] == LUA_REFNIL) {                             \
        lua_pushnumber(L, 0);                                                            \
        lua_pushliteral(L, #rd_type "." #name " error: callback is not set");            \
        return 2;                                                                        \
    }                                                                                    \
                                                                                         \
    int limit = lua_tonumber(L, 2);                                                      \
    void *msg = NULL;                                                                    \
    int count = 0;                                                                       \
    char *err_str = NULL;                                                                \
    while (count < limit) {                                                              \
        msg = queue_pop(rd->event_queues->queues[queue_no]);                             \
        if (msg == NULL)                                                                 \
            break;                                                                       \
                                                                                         \
        count++;                                                                         \
        lua_rawgeti(L, LUA_REGISTRYINDEX, rd->event_queues->cb_refs[queue_no]);          \
        int args_count = push_args_fn(L, msg);                                           \
        if (lua_pcall(L, args_count, 0, 0) != 0) /* call (N arguments, 0 result) */      \
            err_str = (char *)lua_tostring(L, -1);                                       \
        destroy_fn(msg);                                                                 \
                                                                                         \
        if (err_str != NULL)                                                             \
            break;                                                                       \
    }                                                                                    \
    lua_pushinteger(L, count);                                                           \
    if (err_str != NULL)                                                                 \
        lua_pushstring(L, err_str);                                                      \
    else                                                                                 \
        lua_pushnil(L);                                                                  \
                                                                                         \
    return 2;                                                                            \
}

typedef struct {
    queue_t *consume_queue;
    queue_t *delivery_queue;

    queue_t *queues[MAX_QUEUE];
    int cb_refs[MAX_QUEUE];
} event_queues_t;

event_queues_t *new_event_queues();

void destroy_event_queues(struct lua_State *L, event_queues_t *event_queues);

#endif //TNT_KAFKA_CALLBACKS_H
