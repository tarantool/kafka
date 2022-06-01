#include <stdlib.h>

#include <tarantool/module.h>

#include <common.h>

#include <consumer_msg.h>

static const char null_literal[] = "NULL";

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Consumer Message
 */

msg_t *
lua_check_consumer_msg(struct lua_State *L, int index) {
    msg_t **msg_p = (msg_t **)luaL_checkudata(L, index, consumer_msg_label);
    if (msg_p == NULL || *msg_p == NULL)
        luaL_error(L, "Kafka consumer message fatal error: failed to retrieve message from lua stack!");
    return *msg_p;
}

int
lua_consumer_msg_topic(struct lua_State *L) {
    const msg_t *msg = lua_check_consumer_msg(L, 1);
    lua_pushstring(L, rd_kafka_topic_name(msg->topic));
    return 1;
}

int
lua_consumer_msg_partition(struct lua_State *L) {
    const msg_t *msg = lua_check_consumer_msg(L, 1);

    lua_pushnumber(L, (double)msg->partition);
    return 1;
}

int
lua_consumer_msg_offset(struct lua_State *L) {
    msg_t *msg = lua_check_consumer_msg(L, 1);

    luaL_pushint64(L, msg->offset);
    return 1;
}

int
lua_consumer_msg_key(struct lua_State *L) {
    msg_t *msg = lua_check_consumer_msg(L, 1);

    if (msg->key_len <= 0 || msg->key == NULL)
        lua_pushnil(L);
    else
        lua_pushlstring(L, msg->key, msg->key_len);
    return 1;
}

int
lua_consumer_msg_value(struct lua_State *L) {
    const msg_t *msg = lua_check_consumer_msg(L, 1);

    if (msg->value_len <= 0 || msg->value == NULL)
        lua_pushnil(L);
    else
        lua_pushlstring(L, msg->value, msg->value_len);
    return 1;
}

int
lua_consumer_msg_headers(struct lua_State *L) {
    const msg_t *msg = lua_check_consumer_msg(L, 1);
    if (msg->headers == NULL)
        return 0;

    lua_newtable(L);

    size_t idx = 0;
    const char *key;
    const void *val;
    size_t size;

    while (!rd_kafka_header_get_all(msg->headers, idx++,
                                    &key, &val, &size)) {
        lua_pushstring(L, key);
        if (val != NULL)
            lua_pushlstring(L, val, size);
        else
            *(void **)luaL_pushcdata(L, luaL_ctypeid(L, "void *")) = NULL;
        lua_settable(L, -3);
    }
    return 1;
}

int
lua_consumer_msg_tostring(struct lua_State *L) {
    const msg_t *msg = lua_check_consumer_msg(L, 1);

    size_t key_len = msg->key_len <= 0 ? sizeof(null_literal) : msg->key_len + 1;
    char key[key_len];

    if (msg->key_len <= 0 || msg->key == NULL) {
        memcpy(key, null_literal, sizeof(null_literal));
    } else {
        strncpy(key, msg->key, msg->key_len + 1);
        if (key[msg->key_len] != '\0') {
            key[msg->key_len] = '\0';
        }
    }

    size_t value_len = msg->value_len <= 0 ? sizeof(null_literal) : msg->value_len + 1;
    char value[value_len];

    if (msg->value_len <= 0 || msg->value == NULL) {
        memcpy(value, null_literal, sizeof(null_literal));
    } else {
        strncpy(value, msg->value, msg->value_len + 1);
        if (value[msg->value_len] != '\0') {
            value[msg->value_len] = '\0';
        }
    }

    lua_pushfstring(L,
                    "Kafka Consumer Message: topic=%s partition=%d offset=%d key=%s value=%s",
                    rd_kafka_topic_name(msg->topic),
                    msg->partition,
                    msg->offset,
                    key,
                    value);
    return 1;
}

int
lua_consumer_msg_gc(struct lua_State *L) {
    msg_t **msg_p = (msg_t **)luaL_checkudata(L, 1, consumer_msg_label);
    if (msg_p && *msg_p) {
        destroy_consumer_msg(*msg_p);
    }
    if (msg_p)
        *msg_p = NULL;

    return 0;
}

msg_t *
new_consumer_msg(rd_kafka_message_t *rd_message) {
    size_t message_size = sizeof(msg_t) + rd_message->len + rd_message->key_len;
    msg_t *msg = calloc(message_size, 1);
    if (msg == NULL)
        return NULL;

    msg->topic = rd_message->rkt;
    msg->partition = rd_message->partition;
    msg->value = (char*)msg + sizeof(msg_t);
    msg->key = (char*)msg + sizeof(msg_t) + rd_message->len;

    // headers
    rd_kafka_headers_t *hdrsp;
    rd_kafka_resp_err_t err = rd_kafka_message_headers(rd_message, &hdrsp);
    if (err == RD_KAFKA_RESP_ERR_NO_ERROR)
        msg->headers = rd_kafka_headers_copy(hdrsp);

    // value
    if (rd_message->len > 0)
        memcpy(msg->value, rd_message->payload, rd_message->len);
    msg->value_len = rd_message->len;

    // key
    if (rd_message->key_len > 0)
        memcpy(msg->key, rd_message->key, rd_message->key_len);
    msg->key_len = rd_message->key_len;
    msg->offset = rd_message->offset;

    return msg;
}

void
destroy_consumer_msg(msg_t *msg) {
    if (msg == NULL)
        return;
    if (msg->headers != NULL)
        rd_kafka_headers_destroy(msg->headers);
    free(msg);

    return;
}
