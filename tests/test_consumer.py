import os
import time
import asyncio
from contextlib import contextmanager

from aiokafka import AIOKafkaProducer
import tarantool

KAFKA_HOST = os.getenv("KAFKA_HOST", "kafka:9092")


def get_server():
    return tarantool.Connection("127.0.0.1", 3301,
                                user="guest",
                                password=None,
                                socket_timeout=40,
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
    loop = asyncio.get_event_loop()

    async def send():
        producer = AIOKafkaProducer(
            loop=loop, bootstrap_servers='localhost:9092')
        # Get cluster layout and initial topic/partition leadership information
        await producer.start()
        try:
            # Produce message
            for msg in messages:
                await producer.send_and_wait(
                    topic,
                    value=msg['value'].encode('utf-8'),
                    key=msg['key'].encode('utf-8')
                )

        finally:
            # Wait for all pending messages to be delivered or expire.
            await producer.stop()

    loop.run_until_complete(send())


def test_consumer_should_consume_msgs():
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

    write_into_kafka("test_consume", (message1, message2, message3))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_consume_msgs"}):
        server.call("consumer.subscribe", [["test_consume"]])

        response = server.call("consumer.consume", [10])

        assert set(*response) == {
            "test1",
            "test2",
            "test3"
        }


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

        response = server.call("consumer.consume", [10])

        assert set(*response) == {
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

    write_into_kafka("test_unsubscribe", (message1, message2))

    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_completely_unsubscribe_from_topics"}):
        server.call("consumer.subscribe", [["test_unsubscribe"]])

        response = server.call("consumer.consume", [10])

        assert set(*response) == {
            "test1",
            "test2",
        }

        server.call("consumer.unsubscribe", [["test_unsubscribe"]])

        write_into_kafka("test_unsubscribe", (message3, ))

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

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_partially_unsubscribe_from_topics"}):
        server.call("consumer.subscribe", [["test_unsub_partially_1", "test_unsub_partially_2"]])

        write_into_kafka("test_unsub_partially_1", (message1, ))
        write_into_kafka("test_unsub_partially_2", (message2, ))

        # waiting up to 30 seconds
        response = server.call("consumer.consume", [30])

        assert set(*response) == {
            "test1",
            "test2",
        }

        server.call("consumer.unsubscribe", [["test_unsub_partially_1"]])

        write_into_kafka("test_unsub_partially_1", (message3, ))
        write_into_kafka("test_unsub_partially_2", (message4, ))

        response = server.call("consumer.consume", [30])

        assert set(*response) == {"test45"}


def test_consumer_should_log_errors():
    server = get_server()

    with create_consumer(server, "kafka:9090"):
        time.sleep(2)

        response = server.call("consumer.get_errors", [])

        assert len(response.data[0]) > 0


def test_consumer_should_log_debug():
    server = get_server()

    with create_consumer(server, KAFKA_HOST, {"debug": "consumer,cgrp,topic,fetch"}):
        time.sleep(2)

        response = server.call("consumer.get_logs", [])

        assert len(response.data[0]) > 0


def test_consumer_should_log_rebalances():
    server = get_server()

    with create_consumer(server, KAFKA_HOST):
        time.sleep(5)

        server.call("consumer.subscribe", [["test_unsub_partially_1"]])

        time.sleep(20)

        response = server.call("consumer.get_rebalances", [])

        assert len(response.data[0]) > 0


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
        response = server.call("consumer.consume", [30])

        assert set(*response) == {
            "test1",
            "test2",
        }

    time.sleep(2)

    with create_consumer(server, KAFKA_HOST, {"group.id": "should_continue_consuming_from_last_committed_offset"}):
        server.call("consumer.subscribe", [["test_consuming_from_last_committed_offset"]])

        write_into_kafka("test_consuming_from_last_committed_offset", (message3, ))
        write_into_kafka("test_consuming_from_last_committed_offset", (message4, ))

        response = server.call("consumer.consume", [30])

        assert set(*response) == {
            "test3",
            "test4",
        }
