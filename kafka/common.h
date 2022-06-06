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
#include <pthread.h>

#include <tarantool/module.h>

extern const char* const consumer_label;
extern const char* const consumer_msg_label;
extern const char* const producer_label;

int
lua_librdkafka_version(struct lua_State *L);

int
lua_librdkafka_dump_conf(struct lua_State *L, rd_kafka_t *rk);

int
lua_librdkafka_metadata(struct lua_State *L, rd_kafka_t *rk, rd_kafka_topic_t *only_rkt, int timeout_ms);

int
lua_librdkafka_list_groups(struct lua_State *L, rd_kafka_t *rk, const char *group, int timeout_ms);

/**
 * Push native lua error with code -3
 */
int
lua_push_error(struct lua_State *L);

void
set_thread_name(const char *name);

rd_kafka_resp_err_t
kafka_pause(rd_kafka_t *rk);

rd_kafka_resp_err_t
kafka_resume(rd_kafka_t *rk);

#endif //TNT_KAFKA_COMMON_H
