#ifndef TNT_KAFKA_PRODUCER_H
#define TNT_KAFKA_PRODUCER_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

int
lua_producer_tostring(struct lua_State *L);

int
lua_producer_msg_delivery_poll(struct lua_State *L);

int
lua_producer_poll_logs(struct lua_State *L);

int
lua_producer_poll_stats(struct lua_State *L);

int
lua_producer_poll_errors(struct lua_State *L);

int
lua_producer_produce(struct lua_State *L);

int
lua_producer_close(struct lua_State *L);

int
lua_create_producer(struct lua_State *L);

int
lua_producer_destroy(struct lua_State *L);

int
lua_producer_dump_conf(struct lua_State *L);

int
lua_producer_metadata(struct lua_State *L);

int
lua_producer_list_groups(struct lua_State *L);

#endif //TNT_KAFKA_PRODUCER_H
