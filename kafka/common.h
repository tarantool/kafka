#ifndef TNT_KAFKA_COMMON_H
#define TNT_KAFKA_COMMON_H

#include <tarantool/module.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <librdkafka/rdkafka.h>

#include <pthread.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * An x* variant of a memory allocation function calls the original function
 * and panics if it fails (i.e. it should never return NULL).
 */
#define xalloc_impl(size, func, args...)                                        \
    ({                                                                          \
        void *ret = func(args);                                                 \
        if (unlikely(ret == NULL)) {                                            \
            fprintf(stderr, "Can't allocate %zu bytes at %s:%d",                \
                    (size_t)(size), __FILE__, __LINE__);                        \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
        ret;                                                                    \
    })

#define xmalloc(size)           xalloc_impl((size), malloc, (size))
#define xcalloc(n, size)        xalloc_impl((n) * (size), calloc, (n), (size))
#define xrealloc(ptr, size)     xalloc_impl((size), realloc, (ptr), (size))
#define xrd_kafka_topic_partition_list_new(size) xalloc_impl((size), rd_kafka_topic_partition_list_new, (size))
#define xrd_kafka_headers_new(size) xalloc_impl((size), rd_kafka_headers_new, (size))
#define xrd_kafka_topic_conf_new() xalloc_impl(1, rd_kafka_topic_conf_new)
#define xrd_kafka_conf_new() xalloc_impl(1, rd_kafka_conf_new)

static inline void xpthread_fail(const char *what, int rc, const char *file, int line) {
    fprintf(stderr, "%s failed (rc=%d: %s) at %s:%d\n",
            what, rc, strerror(rc), file, line);
    exit(EXIT_FAILURE);
}

#define XPTHREAD(call) do {                            \
    int _rc = (call);                                  \
    if (unlikely(_rc != 0))                            \
        xpthread_fail(#call, _rc, __FILE__, __LINE__); \
} while (0)

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

int
lua_push_kafka_error(struct lua_State *L, rd_kafka_t *rk, rd_kafka_resp_err_t err);

#endif //TNT_KAFKA_COMMON_H
