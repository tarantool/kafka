#include <lua.h>
#include <common.h>

const char* const consumer_label = "__tnt_kafka_consumer";
const char* const consumer_msg_label = "__tnt_kafka_consumer_msg";
const char* const producer_label = "__tnt_kafka_producer";

int
save_pushstring_wrapped(struct lua_State *L) {
    char *str = (char *)lua_topointer(L, 1);
    lua_pushstring(L, str);
    return 1;
}

int
safe_pushstring(struct lua_State *L, char *str) {
    lua_pushcfunction(L, save_pushstring_wrapped);
    lua_pushlightuserdata(L, str);
    return lua_pcall(L, 1, 1, 0);
}

/**
 * Push native lua error with code -3
 */
int
lua_push_error(struct lua_State *L) {
    lua_pushnumber(L, -3);
    lua_insert(L, -2);
    return 2;
}

/**
 * Push current librdkafka version
 */
int
lua_librdkafka_version(struct lua_State *L) {
    const char *version = rd_kafka_version_str();
    lua_pushstring(L, version);
    return 1;
}

int lua_librdkafka_dump_conf(struct lua_State *L, rd_kafka_t *rk) {
    if (rk != NULL) {
        const rd_kafka_conf_t *conf = rd_kafka_conf(rk);
        if (conf == NULL)
            return 0;

        size_t cntp = 0;
        const char **confstr = rd_kafka_conf_dump((rd_kafka_conf_t *)conf, &cntp);
        if (confstr == NULL)
            return 0;

        lua_newtable(L);
        for (size_t i = 0; i < cntp; i += 2) {
            lua_pushstring(L, confstr[i]);
            lua_pushstring(L, confstr[i + 1]);
            lua_settable(L, -3);
        }
        rd_kafka_conf_dump_free(confstr, cntp);
        return 1;
    }
    return 0;
}
