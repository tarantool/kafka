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

log_msg_t *new_log_msg(int level, const char *fac, const char *buf);

void destroy_log_msg(log_msg_t *msg);

void log_callback(const rd_kafka_t *rd_kafka, int level, const char *fac, const char *buf);

/**
 * Handle stats from RDKafka
 */

int stats_callback(rd_kafka_t *rd_kafka, char *json, size_t json_len, void *opaque);


/**
 * Handle errors from RDKafka
 */

typedef struct {
    int err;
    char *reason;
} error_msg_t;

error_msg_t *new_error_msg(int err, const char *reason);

void destroy_error_msg(error_msg_t *msg);

void error_callback(rd_kafka_t *UNUSED(rd_kafka), int err, const char *reason, void *opaque);


/**
 * Handle message delivery reports from RDKafka
 */

typedef struct {
    int dr_callback;
    int err;
} dr_msg_t;

dr_msg_t *new_dr_msg(int dr_callback, int err);

void destroy_dr_msg(dr_msg_t *dr_msg);

void msg_delivery_callback(rd_kafka_t *UNUSED(producer), const rd_kafka_message_t *msg, void *opaque);


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

typedef struct {
    queue_t *consume_queue;
    queue_t *log_queue;
    int      log_cb_ref;
    queue_t *stats_queue;
    int      stats_cb_ref;
    queue_t *error_queue;
    int      error_cb_ref;
    queue_t *delivery_queue;
    queue_t *rebalance_queue;
    int      rebalance_cb_ref;
} event_queues_t;

event_queues_t *new_event_queues();

void destroy_event_queues(struct lua_State *L, event_queues_t *event_queues);

#endif //TNT_KAFKA_CALLBACKS_H
