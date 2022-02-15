#ifndef TNT_KAFKA_CONSUMER_MSG_H
#define TNT_KAFKA_CONSUMER_MSG_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <librdkafka/rdkafka.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Consumer Message
 */
typedef struct {
    rd_kafka_topic_t *topic;
    rd_kafka_headers_t *headers;
    int32_t           partition;
    char              *value;
    size_t            value_len;
    char              *key;
    size_t            key_len;
    int64_t           offset;
} msg_t;

msg_t *lua_check_consumer_msg(struct lua_State *L, int index);

msg_t *new_consumer_msg(rd_kafka_message_t *rd_message);

void destroy_consumer_msg(msg_t *msg);

int lua_consumer_msg_topic(struct lua_State *L);

int lua_consumer_msg_partition(struct lua_State *L);

int lua_consumer_msg_headers(struct lua_State *L);

int lua_consumer_msg_offset(struct lua_State *L);

int lua_consumer_msg_key(struct lua_State *L);

int lua_consumer_msg_value(struct lua_State *L);

int lua_consumer_msg_tostring(struct lua_State *L);

int lua_consumer_msg_gc(struct lua_State *L);

#endif //TNT_KAFKA_CONSUMER_MSG_H
