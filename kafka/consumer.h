#ifndef TNT_KAFKA_CONSUMER_H
#define TNT_KAFKA_CONSUMER_H

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

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

int
lua_consumer_rebalance_protocol(struct lua_State *L);

int
lua_consumer_offsets_for_times(struct lua_State *L);

#endif //TNT_KAFKA_CONSUMER_H
