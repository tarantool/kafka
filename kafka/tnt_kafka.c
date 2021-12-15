#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <common.h>
#include <consumer.h>
#include <consumer_msg.h>
#include <producer.h>

#include <tnt_kafka.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Entry point
 */

LUA_API int
luaopen_kafka_tntkafka(lua_State *L) {
    static const struct luaL_Reg consumer_methods [] = {
            {"subscribe", lua_consumer_subscribe},
            {"unsubscribe", lua_consumer_unsubscribe},
            {"poll_msg", lua_consumer_poll_msg},
            {"poll_logs", lua_consumer_poll_logs},
            {"poll_stats", lua_consumer_poll_stats},
            {"poll_errors", lua_consumer_poll_errors},
            {"poll_rebalances", lua_consumer_poll_rebalances},
            {"store_offset", lua_consumer_store_offset},
            {"dump_conf", lua_consumer_dump_conf},
            {"close", lua_consumer_close},
            {"destroy", lua_consumer_destroy},
            {"__tostring", lua_consumer_tostring},
            {NULL, NULL}
    };

    luaL_newmetatable(L, consumer_label);
    lua_pushvalue(L, -1);
    luaL_register(L, NULL, consumer_methods);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, consumer_label);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);

    static const struct luaL_Reg consumer_msg_methods [] = {
            {"topic", lua_consumer_msg_topic},
            {"partition", lua_consumer_msg_partition},
            {"offset", lua_consumer_msg_offset},
            {"key", lua_consumer_msg_key},
            {"value", lua_consumer_msg_value},
            {"__tostring", lua_consumer_msg_tostring},
            {"__gc", lua_consumer_msg_gc},
            {NULL, NULL}
    };

    luaL_newmetatable(L, consumer_msg_label);
    lua_pushvalue(L, -1);
    luaL_register(L, NULL, consumer_msg_methods);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, consumer_msg_label);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);

    static const struct luaL_Reg producer_methods [] = {
            {"produce", lua_producer_produce},
            {"msg_delivery_poll", lua_producer_msg_delivery_poll},
            {"poll_logs", lua_producer_poll_logs},
            {"poll_stats", lua_producer_poll_stats},
            {"poll_errors", lua_producer_poll_errors},
            {"dump_conf", lua_producer_dump_conf},
            {"close", lua_producer_close},
            {"destroy", lua_producer_destroy},
            {"__tostring", lua_producer_tostring},
            {NULL, NULL}
    };

    luaL_newmetatable(L, producer_label);
    lua_pushvalue(L, -1);
    luaL_register(L, NULL, producer_methods);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, producer_label);
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);

	lua_newtable(L);
	static const struct luaL_Reg meta [] = {
        {"create_consumer", lua_create_consumer},
        {"create_producer", lua_create_producer},
        {"librdkafka_version", lua_librdkafka_version},
        {NULL, NULL}
	};
	luaL_register(L, NULL, meta);
	return 1;
}
