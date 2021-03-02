#include <stdlib.h>

#include <tarantool/module.h>

#include <common.h>

#include <consumer_msg.h>

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
    msg_t *msg = lua_check_consumer_msg(L, 1);
    lua_pushstring(L, rd_kafka_topic_name(msg->topic));
    return 1;
}

int
lua_consumer_msg_partition(struct lua_State *L) {
    msg_t *msg = lua_check_consumer_msg(L, 1);

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

    if (msg->key_len <= 0 || msg->key == NULL || ((char*)msg->key) == NULL) {
        return 0;
    }

    lua_pushlstring(L, msg->key, msg->key_len);
    return 1;
}

int
lua_consumer_msg_value(struct lua_State *L) {
    msg_t *msg = lua_check_consumer_msg(L, 1);

    if (msg->value_len <= 0 || msg->value == NULL || ((char*)msg->value) == NULL) {
        return 0;
    }

    lua_pushlstring(L, msg->value, msg->value_len);
    return 1;
}

int
lua_consumer_msg_tostring(struct lua_State *L) {
    msg_t *msg = lua_check_consumer_msg(L, 1);

    size_t key_len = msg->key_len <= 0 ? 5: msg->key_len + 1;
    char key[key_len];

    if (msg->key_len <= 0 || msg->key == NULL || ((char*)msg->key) == NULL) {
        strncpy(key, "NULL", 5);
    } else {
        strncpy(key, msg->key, msg->key_len + 1);
        if (key[msg->key_len] != '\0') {
            key[msg->key_len] = '\0';
        }
    }

    size_t value_len = msg->value_len <= 0 ? 5: msg->value_len + 1;
    char value[value_len];

    if (msg->value_len <= 0 || msg->value == NULL || ((char*)msg->value) == NULL) {
        strncpy(value, "NULL", 5);
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

void
lua_consumer_msg_print(msg_t *msg) {
    size_t key_len = msg->key_len <= 0 ? 5: msg->key_len + 1;
    char key[key_len];

    if (msg->key_len <= 0 || msg->key == NULL || ((char*)msg->key) == NULL) {
        strncpy(key, "NULL", 5);
    } else {
        strncpy(key, msg->key, msg->key_len + 1);
        if (key[msg->key_len] != '\0') {
            key[msg->key_len] = '\0';
        }
    }

    size_t value_len = msg->value_len <= 0 ? 5: msg->value_len + 1;
    char value[value_len];

    if (msg->value_len <= 0 || msg->value == NULL || ((char*)msg->value) == NULL) {
        strncpy(value, "NULL", 5);
    } else {
        strncpy(value, msg->value, msg->value_len + 1);
        if (value[msg->value_len] != '\0') {
            value[msg->value_len] = '\0';
        }
    }

#ifdef __APPLE__
#define PRI64 "lld"
#else
#define PRI64 "ld"
#endif
    printf("Kafka Consumer Message: topic=%s partition=%d offset=%"PRI64" key=%s value=%s\n",
           rd_kafka_topic_name(msg->topic),
           msg->partition,
           msg->offset,
           key,
           value);
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
    msg_t *msg;
    msg = malloc(sizeof(msg_t));
    msg->topic = rd_message->rkt;
    msg->partition = rd_message->partition;
    msg->value = NULL;
    msg->value_len = 0;
    msg->key = NULL;
    msg->key_len = 0;
    msg->offset = 0;

    // value
    if (rd_message->len > 0) {
        msg->value = malloc(rd_message->len);
        memcpy(msg->value, rd_message->payload, rd_message->len);
    }
    msg->value_len = rd_message->len;

    // key
    if (rd_message->key_len > 0) {
        msg->key = malloc(rd_message->key_len);
        memcpy(msg->key, rd_message->key, rd_message->key_len);
    }
    msg->key_len = rd_message->key_len;

    msg->offset = rd_message->offset;

    return msg;
}

void
destroy_consumer_msg(msg_t *msg) {
    if (msg == NULL) {
        return;
    }

    if (msg->key != NULL) {
        free(msg->key);
    }

    if (msg->value != NULL) {
        free(msg->value);
    }

    free(msg);

    return;
}
