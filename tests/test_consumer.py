import os
import time
import json
import asyncio
from contextlib import contextmanager
import random
import string

import pytest
from aiokafka import AIOKafkaProducer
import tarantool

KAFKA_HOST = os.getenv("KAFKA_HOST", "kafka:9092")


def randomword(length):
    letters = string.ascii_lowercase
    return ''.join(random.choice(letters) for i in range(length))


def get_message_values(messages):
    result = []
    for msg in messages:
        if 'value' in msg:
            result.append(msg['value'])
    return result


def get_server():
    return tarantool.Connection("127.0.0.1", 3301,
                                user="guest",
                                password=None,
                                socket_timeout=40,
                                connection_timeout=40,
                                reconnect_max_attempts=3,
                                reconnect_delay=1,
                                connect_now=True)


@contextmanager
def create_consumer(server, *args):
    try:
        server.call("consumer.create", args)
        yield

    finally:
        server.call("consumer.close", [])


def write_into_kafka(topic, messages):
    loop = asyncio.get_event_loop_policy().new_event_loop()

    async def send():
        producer = AIOKafkaProducer(bootstrap_servers='localhost:9092')
        # Get cluster layout and initial topic/partition leadership information
        await producer.start()
        try:
            # Produce message
            for msg in messages:
                headers = None
                if 'headers' in msg:
                    headers = []
                    for k, v in msg['headers'].items():
                        headers.append((k, v.encode('utf-8') if v is not None else v))
                await producer.send_and_wait(
                    topic,
                    value=msg['value'].encode('utf-8'),
                    key=msg['key'].encode('utf-8'),
                    headers=headers,
                )

        finally:
            # Wait for all pending messages to be delivered or expire.
            await producer.stop()

    loop.run_until_complete(send())
    loop.close()


def test_consumer_should_consume_msgs():
    message1 = {
        "key": "test1",
        "value": "test1",
    }

    message2 = {
        "key": "test1",
        "value": "test2",
    }

    message3 = {
        "key": "test1",
        "value": "test3",
        "headers": {"key1": "value1", "key2": "value2", "nullable": None},
    }

    message4 = {
        "key": "",
        "value": "test4",
    }

    message5 = {
        "key": "",
        "value": "",
    }

    write_into_kafka("test_consume", (
        message1,
        message2,
        message3,
        message4,
        message5,
    ))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_consume_msgs"}):
        server.call("consumer.subscribe", [["test_consume"]])

        response = server.call("consumer.consume", [10])[0]

        assert set(get_message_values(response)) == {
            "test1",
            "test2",
            "test3",
            "test4",
        }

        for msg in filter(lambda x: 'value' in x, response):
            if msg['value'] == 'test1':
                assert msg['key'] == 'test1'
            elif msg['value'] == 'test3':
                assert msg['headers'] == {'key1': 'value1', 'key2': 'value2', 'nullable': None}


def test_consumer_seek_partitions():
    key = "test_seek_unique_key"
    value = "test_seek_unique_value"
    message = {
        "key": key,
        "value": value,
    }

    topic = 'test_consumer_seek' + randomword(15)
    write_into_kafka(topic, (message,))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {'group.id': 'consumer_seek'}):
        server.call('consumer.subscribe', [[topic]])

        response = server.call("consumer.test_seek_partitions")
        assert len(response[0]) == 5

        for item in response[0]:
            assert item['key'] == key
            assert item['value'] == value


def test_consumer_create_errors():
    server = get_server()
    server.call("consumer.test_create_errors")


def test_consumer_should_consume_msgs_from_multiple_topics():
    message1 = {
        "key": "test1",
        "value": "test1"
    }

    message2 = {
        "key": "test1",
        "value": "test2"
    }

    message3 = {
        "key": "test1",
        "value": "test33"
    }

    write_into_kafka("test_multi_consume_1", (message1, message2))
    write_into_kafka("test_multi_consume_2", (message3, ))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_consume_msgs_from_multiple_topics"}):
        server.call("consumer.subscribe", [["test_multi_consume_1", "test_multi_consume_2"]])

        response = server.call("consumer.consume", [10])[0]

        assert set(get_message_values(response)) == {
            "test1",
            "test2",
            "test33"
        }


def test_consumer_should_completely_unsubscribe_from_topics():
    message1 = {
        "key": "test1",
        "value": "test1"
    }

    message2 = {
        "key": "test1",
        "value": "test2"
    }

    message3 = {
        "key": "test1",
        "value": "test34"
    }

    t = f"test_unsubscribe_{randomword(4)}"
    write_into_kafka(t, (message1, message2))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {
        "group.id": "should_completely_unsubscribe_from_topics",
    }):
        server.call("consumer.subscribe", [[t]])

        response = server.call("consumer.consume", [10])[0]

        assert set(get_message_values(response)) == {
            "test1",
            "test2",
        }

        server.call("consumer.unsubscribe", [[t]])

        write_into_kafka(t, (message3, ))

        response = server.call("consumer.consume", [10])

        assert set(*response) == set()


def test_consumer_should_partially_unsubscribe_from_topics():
    message1 = {
        "key": "test1",
        "value": "test1"
    }

    message2 = {
        "key": "test1",
        "value": "test2"
    }

    message3 = {
        "key": "test1",
        "value": "test35"
    }

    message4 = {
        "key": "test1",
        "value": "test45"
    }

    server = get_server()

    salt = randomword(4)
    with create_consumer(server, KAFKA_HOST, {
        "group.id": f"should_partially_unsubscribe_from_topics_{salt}",
    }):
        t1 = f"test_unsub_partially_{salt}_1"
        t2 = f"test_unsub_partially_{salt}_2"

        # Ensure topics exist BEFORE subscribe/rebalance (auto-create can lag in CI)
        write_into_kafka(t1, (message1, ))
        write_into_kafka(t2, (message2, ))
        server.call("consumer.subscribe", [[t1, t2]])
        time.sleep(5)  # give group join/rebalance time

        # waiting up to 30 seconds
        response = server.call("consumer.consume", [30])[0]

        assert set(get_message_values(response)) == {
            "test1",
            "test2",
        }

        server.call("consumer.unsubscribe", [[t1]])
        time.sleep(2)  # let revoke/apply subscription update settle

        write_into_kafka(t1, (message3, ))
        write_into_kafka(t2, (message4, ))
        time.sleep(5)

        response = server.call("consumer.consume", [30])[0]

        assert set(get_message_values(response)) == {"test45"}


def test_consumer_should_log_errors():
    server = get_server()

    with create_consumer(server, "kafka:9090"):
        time.sleep(5)

        response = server.call("consumer.get_errors", [])

        assert len(response.data[0]) > 0


def test_consumer_stats():
    server = get_server()

    with create_consumer(server, "kafka:9090"):
        time.sleep(2)

        response = server.call("consumer.get_stats", [])
        assert len(response) > 0
        found = False
        for resp in response:
            if len(resp) == 0:
                continue

            found = True
            stat = json.loads(resp[0])

            assert 'rdkafka#consumer' in stat['name']
            assert 'kafka:9090/bootstrap' in stat['brokers']
            assert stat['type'] == 'consumer'
            break

        assert found


def test_consumer_dump_conf():
    server = get_server()

    with create_consumer(server, "kafka:9090"):
        time.sleep(2)

        response = server.call("consumer.dump_conf", [])
        assert len(response) > 0
        assert len(response[0]) > 0
        assert 'session.timeout.ms' in response[0]
        assert 'socket.max.fails' in response[0]
        assert 'compression.codec' in response[0]


def test_consumer_metadata():
    server = get_server()

    with create_consumer(server, KAFKA_HOST):
        time.sleep(2)

        response = server.call("consumer.metadata", [])
        assert 'orig_broker_name' in response[0]
        assert 'orig_broker_id' in response[0]
        assert 'brokers' in response[0]
        assert 'topics' in response[0]
        assert 'host' in response[0]['brokers'][0]
        assert 'port' in response[0]['brokers'][0]
        assert 'id' in response[0]['brokers'][0]

        response = server.call("consumer.metadata", [0])
        assert tuple(response) == (None, 'Local: Timed out')

        response = server.call("consumer.list_groups", [])
        assert response[0] is not None
        response = server.call("consumer.list_groups", [0])
        assert tuple(response) == (None, 'Local: Timed out')

    with create_consumer(server, "badhost:9090"):
        response = server.call("consumer.metadata", [0])
        assert tuple(response) == (None, 'Local: Broker transport failure')

        response = server.call("consumer.metadata", [0])
        assert tuple(response) == (None, 'Local: Broker transport failure')


def test_consumer_should_log_debug():
    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"debug": "consumer,cgrp,topic,fetch"}):
        time.sleep(2)

        response = server.call("consumer.get_logs", [])

        assert len(response.data[0]) > 0


def test_consumer_should_log_rebalances():
    # Use unique topic and create it before subscribe: in CI topic auto-create can lag,
    # and subscribing to a non-existent topic may yield no assignment/rebalance events.
    topic = f"test_rebalances_{randomword(6)}"
    write_into_kafka(topic, ({"key": "init", "value": "init"},))

    server = get_server()
    gid = f"g_rebalances_{randomword(6)}"
    with create_consumer(server, KAFKA_HOST, {"group.id": gid}):
        time.sleep(2)
        server.call("consumer.subscribe", [[topic]])
        time.sleep(10)
        response = server.call("consumer.get_rebalances", [])
        assert len(response.data[0]) > 0


def test_consumer_rebalance_protocol():
    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"bootstrap.servers": KAFKA_HOST}):
        time.sleep(5)
        response = server.call("consumer.rebalance_protocol", [])
        assert response[0] == 'NONE'

        # Ensure topic exists before subscribe (auto-create can lag)
        topic = f"test_rebalance_proto_{randomword(6)}"
        write_into_kafka(topic, ({"key": "init", "value": "init"},))

        server.call("consumer.subscribe", [[topic]])
        response = server.call("consumer.rebalance_protocol", [])
        assert response[0] == 'NONE'


def test_consumer_should_continue_consuming_from_last_committed_offset():
    message1 = {
        "key": "test1",
        "value": "test1"
    }

    message2 = {
        "key": "test1",
        "value": "test2"
    }

    message3 = {
        "key": "test1",
        "value": "test3"
    }

    message4 = {
        "key": "test1",
        "value": "test4"
    }

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_continue_consuming_from_last_committed_offset"}):
        server.call("consumer.subscribe", [["test_consuming_from_last_committed_offset"]])

        write_into_kafka("test_consuming_from_last_committed_offset", (message1, ))
        write_into_kafka("test_consuming_from_last_committed_offset", (message2, ))

        # waiting up to 30 seconds
        response = server.call("consumer.consume", [30])[0]

        assert set(get_message_values(response)) == {
            "test1",
            "test2",
        }

    time.sleep(2)

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_continue_consuming_from_last_committed_offset"}):
        server.call("consumer.subscribe", [["test_consuming_from_last_committed_offset"]])

        write_into_kafka("test_consuming_from_last_committed_offset", (message3, ))
        write_into_kafka("test_consuming_from_last_committed_offset", (message4, ))

        response = server.call("consumer.consume", [30])[0]

        assert set(get_message_values(response)) == {
            "test3",
            "test4",
        }


def test_consumer_pause_resume():
    message_before_pause = {
        "key": "message_before_pause",
        "value": "message_before_pause",
    }

    message_on_pause = {
        "key": "message_on_pause",
        "value": "message_on_pause",
    }

    message_after_pause = {
        "key": "message_after_pause",
        "value": "message_after_pause",
    }

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_consume_msgs"}):
        topic = "test_resume_pause"
        # Ensure topic exists before subscribe/rebalance
        write_into_kafka(topic, (message_before_pause,))
        server.call("consumer.subscribe", [[topic]])

        response = server.call("consumer.consume", [10])[0]

        assert set(get_message_values(response)) == {
            "message_before_pause",
        }

        response = server.call("consumer.pause")
        assert len(response) == 0

        write_into_kafka("test_resume_pause", (message_on_pause,))
        response = server.call("consumer.consume", [2])[0]
        assert len(response) == 0

        response = server.call("consumer.resume")
        assert len(response) == 0
        write_into_kafka("test_resume_pause", (message_after_pause,))

        response = server.call("consumer.consume", [2])[0]
        assert set(get_message_values(response)) == {
            "message_on_pause",
            "message_after_pause",
        }


@pytest.mark.timeout(5)
def test_consumer_should_be_closed():
    server = get_server()

    with create_consumer(server, '127.0.0.1:12345', {"group.id": None}):
        pass


def test_offsets_for_times_api():
    topic = "test_offsets_for_times_api"
    tag = randomword(6)

    batch1 = [{"key": f"b1-{tag}-{i}", "value": f"v1-{tag}-{i}"} for i in range(3)]
    write_into_kafka(topic, batch1)

    time.sleep(2)
    ts_cut_ms = int(time.time() * 1000)

    batch2 = [{"key": f"b2-{tag}-{i}", "value": f"v2-{tag}-{i}"} for i in range(2)]
    write_into_kafka(topic, batch2)

    server = get_server()
    group_id = f"g_offsets_for_times_api_{tag}"

    with create_consumer(server, KAFKA_HOST, {"group.id": group_id}):
        res = server.call("consumer.offsets_for_times", [[topic, 0, ts_cut_ms], ['invalid', 1000, ts_cut_ms]], 3000)
        assert len(res) == 1 and len(res[0]) == 2

        item = res[0][0]
        assert item.get("topic") == topic
        assert item.get("partition") == 0
        assert "error_code" in item, item
        assert item["error_code"] == 0
        assert "error" not in item
        assert isinstance(item.get("offset"), int) and item["offset"] >= 0

        item = res[0][1]
        assert item.get("topic") == "invalid"
        assert item["error"] == "Broker: Unknown topic or partition"
        assert item["error_code"] != 0


def test_offsets_for_times_seek_from_cut():
    topic = "test_offsets_for_times"
    tag = randomword(6)

    batch1 = [{"key": f"b1-{tag}-{i}", "value": f"v1-{tag}-{i}"} for i in range(5)]
    write_into_kafka(topic, batch1)

    time.sleep(2)
    ts_cut_ms = int(time.time() * 1000)

    batch2 = [{"key": f"b2-{tag}-{i}", "value": f"v2-{tag}-{i}"} for i in range(5)]
    write_into_kafka(topic, batch2)

    server = get_server()
    group_id = f"g_seek_from_time_{tag}"

    with create_consumer(server, KAFKA_HOST, {"group.id": group_id}):
        server.call("consumer.subscribe", [[topic]])

        time.sleep(10)

        res = server.call("consumer.seek_from_time", [topic, ts_cut_ms, 5000])
        assert len(res) == 1

        applied = res[0]
        assert isinstance(applied, list) and len(applied) >= 1, f"no seeks applied: {applied}"

        for item in applied:
            assert isinstance(item, list) and len(item) == 3, item
            t, p, o = item
            assert t == topic, f"unexpected topic in seek: {item}"
            assert isinstance(p, int)
            assert isinstance(o, int) and o != -1001, f"invalid offset in seek: {item}"

        msgs = server.call("consumer.consume", [4])[0]
        values = set(get_message_values(msgs))

        want = {m["value"] for m in batch2}
        not_want = {m["value"] for m in batch1}

        assert values.issuperset(want), f"missing: {want - values}, got={values}"
        assert values.isdisjoint(not_want), f"unexpected (batch1) values present: {values & not_want}"
