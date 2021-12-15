#ifndef TNT_KAFKA_COMMON_H
#define TNT_KAFKA_COMMON_H

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <librdkafka/rdkafka.h>

#include <tarantool/module.h>

const char* const consumer_label;
const char* const consumer_msg_label;
const char* const producer_label;

int save_pushstring_wrapped(struct lua_State *L);

int safe_pushstring(struct lua_State *L, char *str);

int lua_librdkafka_version(struct lua_State *L);

int lua_librdkafka_dump_conf(struct lua_State *L, rd_kafka_t *rk);

/**
 * Push native lua error with code -3
 */
int lua_push_error(struct lua_State *L);

#endif //TNT_KAFKA_COMMON_H
