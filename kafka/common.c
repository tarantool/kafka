#define _GNU_SOURCE

#include <lua.h>
#include <common.h>
#include <assert.h>

const char* const consumer_label = "__tnt_kafka_consumer";
const char* const consumer_msg_label = "__tnt_kafka_consumer_msg";
const char* const producer_label = "__tnt_kafka_producer";

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

int
lua_librdkafka_dump_conf(struct lua_State *L, rd_kafka_t *rk) {
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

static ssize_t
wait_librdkafka_metadata(va_list args) {
    rd_kafka_t *rk = va_arg(args, rd_kafka_t *);
    int all_topics = va_arg(args, int);
    rd_kafka_topic_t *only_rkt = va_arg(args, rd_kafka_topic_t *);
    const struct rd_kafka_metadata **metadatap = va_arg(args, const struct rd_kafka_metadata **);
    int timeout_ms = va_arg(args, int);
    return rd_kafka_metadata(rk, all_topics, only_rkt, metadatap, timeout_ms);
}

int
lua_librdkafka_metadata(struct lua_State *L, rd_kafka_t *rk, rd_kafka_topic_t *only_rkt, int timeout_ms) {
    assert(rk != NULL);

    int all_topics = 0;
    if (only_rkt == NULL)
        all_topics = 1;

    const struct rd_kafka_metadata *metadatap;
    rd_kafka_resp_err_t err = coio_call(wait_librdkafka_metadata, rk, all_topics, only_rkt, &metadatap, timeout_ms);

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        lua_pushnil(L);
        lua_pushstring(L, rd_kafka_err2str(err));
        return 2;
    }

    lua_newtable(L); // metadata

    lua_pushliteral(L, "brokers"); // metadata.brokers
    lua_createtable(L, metadatap->broker_cnt, 0);
    for (int i = 0; i < metadatap->broker_cnt; i++) {
        lua_pushnumber(L, i + 1); // metadata.brokers[i]
        lua_createtable(L, 0, 3);

        lua_pushliteral(L, "id"); // metadata.brokers[i].id
        lua_pushnumber(L, metadatap->brokers[i].id);
        lua_settable(L, -3);

        lua_pushliteral(L, "port"); // metadata.brokers[i].port
        lua_pushnumber(L, metadatap->brokers[i].port);
        lua_settable(L, -3);

        lua_pushliteral(L, "host"); // metadata.brokers[i].host
        lua_pushstring(L, metadatap->brokers[i].host);
        lua_settable(L, -3);

        lua_settable(L, -3); // metadata.brokers[i]
    }

    lua_settable(L, -3); // metadata.brokers

    lua_pushliteral(L, "topics"); // metadata.topics
    lua_createtable(L, metadatap->topic_cnt, 0);
    for (int i = 0; i < metadatap->topic_cnt; i++) {
        lua_pushnumber(L, i + 1); // metadata.topics[i]
        lua_createtable(L, 0, 4);

        lua_pushliteral(L, "topic"); // metadata.topics[i].topic
        lua_pushstring(L, metadatap->topics[i].topic);
        lua_settable(L, -3);

        lua_pushliteral(L, "partitions"); // metadata.topics[i].partitions
        lua_createtable(L, 0, metadatap->topics[i].partition_cnt);

        for (int j = 0; j < metadatap->topics[i].partition_cnt; j++) {
            lua_pushnumber(L, j + 1); // metadata.topics[i].partitions[j]
            lua_createtable(L, 0, 8);

            lua_pushliteral(L, "id"); // metadata.topics[i].partitions[j].id
            lua_pushnumber(L, metadatap->topics[i].partitions[j].id);
            lua_settable(L, -3);

            lua_pushliteral(L, "leader"); // metadata.topics[i].partitions[j].leader
            lua_pushnumber(L, metadatap->topics[i].partitions[j].leader);
            lua_settable(L, -3);

            if (metadatap->topics[i].partitions[j].err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                lua_pushliteral(L, "error_code"); // metadata.topics[i].partitions[j].error_code
                lua_pushnumber(L, metadatap->topics[i].partitions[j].err);
                lua_settable(L, -3);

                lua_pushliteral(L, "error"); // metadata.topics[i].partitions[j].error
                lua_pushstring(L, rd_kafka_err2str(metadatap->topics[i].partitions[j].err));
                lua_settable(L, -3);
            }

            lua_pushliteral(L, "isr"); // metadata.topics[i].partitions[j].isr
            lua_createtable(L, metadatap->topics[i].partitions[j].isr_cnt, 0);
            for (int k = 0; k < metadatap->topics[i].partitions[j].isr_cnt; k++) {
                lua_pushnumber(L, k + 1); // metadata.topics[i].partitions[j].isr[k]
                lua_pushnumber(L, metadatap->topics[i].partitions[j].isrs[k]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3); // metadata.topics[i].partitions[j].isr

            lua_pushliteral(L, "replicas"); // metadata.topics[i].partitions[j].replicas
            lua_createtable(L, metadatap->topics[i].partitions[j].replica_cnt, 0);
            for (int k = 0; k < metadatap->topics[i].partitions[j].replica_cnt; k++) {
                lua_pushnumber(L, k + 1); // metadata.topics[i].partitions[j].replicas[k]
                lua_pushnumber(L, metadatap->topics[i].partitions[j].replicas[k]);
                lua_settable(L, -3);
            }
            lua_settable(L, -3); // metadata.topics[i].partitions[j].replicas
            lua_settable(L, -3); // metadata.topics[i].partitions[j]
        }

        lua_settable(L, -3); // metadata.topics[i].partitions

        if (metadatap->topics[i].err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            lua_pushliteral(L, "error_code"); // metadata.topics[i].error_code
            lua_pushnumber(L, metadatap->topics[i].err);
            lua_settable(L, -3);

            lua_pushliteral(L, "error"); // metadata.topics[i].error
            lua_pushstring(L, rd_kafka_err2str(metadatap->topics[i].err));
            lua_settable(L, -3);
        }

        lua_settable(L, -3); // metadata.topics[i]
    }
    lua_settable(L, -3); // metadata.topics

    lua_pushliteral(L, "orig_broker_id"); // metadata.orig_broker_id
    lua_pushinteger(L, metadatap->orig_broker_id);
    lua_settable(L, -3);

    lua_pushliteral(L, "orig_broker_name"); // metadata.orig_broker_name
    lua_pushstring(L, metadatap->orig_broker_name);
    lua_settable(L, -3);

    rd_kafka_metadata_destroy(metadatap);
    return 1;
}

static ssize_t
wait_librdkafka_list_groups(va_list args) {
    rd_kafka_t *rk = va_arg(args, rd_kafka_t *);
    const char *group = va_arg(args, const char *);
    const struct rd_kafka_group_list **grplistp = va_arg(args, const struct rd_kafka_group_list **);
    int timeout_ms = va_arg(args, int);
    return rd_kafka_list_groups(rk, group, grplistp, timeout_ms);
}

int
lua_librdkafka_list_groups(struct lua_State *L, rd_kafka_t *rk, const char *group, int timeout_ms) {
    const struct rd_kafka_group_list *grplistp;
    rd_kafka_resp_err_t err = coio_call(wait_librdkafka_list_groups, rk, group, &grplistp, timeout_ms);

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        lua_pushnil(L);
        lua_pushstring(L, rd_kafka_err2str(err));
        return 2;
    }

    lua_createtable(L, grplistp->group_cnt, 0);
    for (int i = 0; i < grplistp->group_cnt; i++) {
        lua_pushnumber(L, i + 1);
        lua_createtable(L, 0, 8);

        lua_pushliteral(L, "broker");
        lua_createtable(L, 0, 3);

        lua_pushliteral(L, "id");
        lua_pushnumber(L, grplistp->groups[i].broker.id);
        lua_settable(L, -3);

        lua_pushliteral(L, "port");
        lua_pushnumber(L, grplistp->groups[i].broker.port);
        lua_settable(L, -3);

        lua_pushliteral(L, "host");
        lua_pushstring(L, grplistp->groups[i].broker.host);
        lua_settable(L, -3);

        lua_settable(L, -3);

        lua_pushstring(L, "group");
        lua_pushstring(L, grplistp->groups[i].group);
        lua_settable(L, -3);

        if (grplistp->groups[i].err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            lua_pushliteral(L, "error_code");
            lua_pushnumber(L, grplistp->groups[i].err);
            lua_settable(L, -3);

            lua_pushliteral(L, "error");
            lua_pushstring(L, rd_kafka_err2str(grplistp->groups[i].err));
            lua_settable(L, -3);
        }

        lua_pushliteral(L, "state");
        lua_pushstring(L, grplistp->groups[i].state);
        lua_settable(L, -3);

        lua_pushliteral(L, "protocol_type");
        lua_pushstring(L, grplistp->groups[i].protocol_type);
        lua_settable(L, -3);

        lua_pushliteral(L, "protocol");
        lua_pushstring(L, grplistp->groups[i].protocol);
        lua_settable(L, -3);

        lua_pushliteral(L, "members");
        lua_createtable(L, grplistp->groups[i].member_cnt, 0);
        for (int j = 0; j < grplistp->groups[i].member_cnt; j++) {
            lua_pushnumber(L, j + 1);
            lua_createtable(L, 0, 8);

            lua_pushliteral(L, "member_id");
            lua_pushstring(L, grplistp->groups[i].members[j].member_id);
            lua_settable(L, -3);

            lua_pushliteral(L, "client_id");
            lua_pushstring(L, grplistp->groups[i].members[j].client_id);
            lua_settable(L, -3);

            lua_pushliteral(L, "client_host");
            lua_pushstring(L, grplistp->groups[i].members[j].client_host);
            lua_settable(L, -3);

            lua_pushliteral(L, "member_metadata");
            lua_pushlstring(L,
                   grplistp->groups[i].members[j].member_metadata,
                   grplistp->groups[i].members[j].member_metadata_size);
            lua_settable(L, -3);

            lua_pushliteral(L, "member_assignment");
            lua_pushlstring(L,
                   grplistp->groups[i].members[j].member_assignment,
                   grplistp->groups[i].members[j].member_assignment_size);
            lua_settable(L, -3);

            lua_settable(L, -3);
        }
        lua_settable(L, -3);

        lua_settable(L, -3);
    }

    rd_kafka_group_list_destroy(grplistp);
    return 1;
}

#ifdef __linux__
void set_thread_name(pthread_t thread, const char *name) {
    int rc = pthread_setname_np(thread, name);
    (void)rc;
    assert(rc == 0);
}
#else
void set_thread_name(pthread_t thread, const char *name) {
    (void)thread;
    (void)name;
}
#endif
