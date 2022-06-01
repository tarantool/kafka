#ifndef TNT_KAFKA_CONSUMER_H
#define TNT_KAFKA_CONSUMER_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

#include <common.h>
#include <queue.h>
#include <callbacks.h>
#include <consumer_msg.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Consumer
 */

typedef struct {
    rd_kafka_t      *rd_consumer;
    pthread_t       thread;
    pthread_attr_t  attr;
    int             should_stop;
    pthread_mutex_t lock;
} consumer_poller_t;

typedef struct {
    rd_kafka_t                      *rd_consumer;
    rd_kafka_topic_partition_list_t *topics;
    event_queues_t                  *event_queues;
    consumer_poller_t               *poller;
} consumer_t;

int
lua_consumer_subscribe(struct lua_State *L);

int
lua_consumer_unsubscribe(struct lua_State *L);

int
lua_consumer_tostring(struct lua_State *L);

int
lua_consumer_poll_msg(struct lua_State *L);

int
lua_consumer_poll_logs(struct lua_State *L);

int
lua_consumer_poll_stats(struct lua_State *L);

int
lua_consumer_poll_errors(struct lua_State *L);

int
lua_consumer_poll_rebalances(struct lua_State *L);

int
lua_consumer_store_offset(struct lua_State *L);

int
lua_consumer_seek_partitions(struct lua_State *L);

int
lua_consumer_close(struct lua_State *L);

int
lua_consumer_destroy(struct lua_State *L);

int
lua_create_consumer(struct lua_State *L);

int
lua_consumer_dump_conf(struct lua_State *L);

int
lua_consumer_metadata(struct lua_State *L);

int
lua_consumer_list_groups(struct lua_State *L);

int
lua_consumer_pause(struct lua_State *L);

int
lua_consumer_resume(struct lua_State *L);

#endif //TNT_KAFKA_CONSUMER_H
