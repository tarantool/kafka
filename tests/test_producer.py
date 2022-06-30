import os
import time
import json
import asyncio

from aiokafka import AIOKafkaConsumer
import tarantool

KAFKA_HOST = os.getenv("KAFKA_HOST", "kafka:9092")


def get_server():
    return tarantool.Connection("127.0.0.1", 3301,
                                user="guest",
                                password=None,
                                socket_timeout=10,
                                connection_timeout=40,
                                reconnect_max_attempts=3,
                                reconnect_delay=1,
                                connect_now=True)


def test_producer_should_produce_msgs():
    server = get_server()

    server.call("producer.create", [KAFKA_HOST])

    messages = [
        {'key': '1', 'value': '1'},
        {'key': '2', 'value': '2'},
        {'key': '3', 'value': '3'},
        {'key': '4', 'value': '4', 'headers': {'header1_key': 'header1_value', 'header2_key': 'header2_value'}},
    ]
    server.call("producer.produce", [messages])

    loop = asyncio.get_event_loop_policy().new_event_loop()

    async def test():
        kafka_output = []

        async def consume():
            consumer = AIOKafkaConsumer(
                'test_producer',
                group_id="test_group",
                bootstrap_servers='localhost:9092',
                auto_offset_reset="earliest",
            )
            # Get cluster layout
            await consumer.start()

            try:
                # Consume messages
                async for msg in consumer:
                    kafka_msg = {
                        'key': msg.key if msg.key is None else msg.key.decode('utf8'),
                        'value': msg.value if msg.value is None else msg.value.decode('utf8')
                    }
                    if msg.headers:
                        kafka_msg['headers'] = {}
                        for k, v in msg.headers:
                            kafka_msg['headers'][k] = v.decode('utf8')
                    kafka_output.append(kafka_msg)

            finally:
                # Will leave consumer group; perform autocommit if enabled.
                await consumer.stop()

        try:
            await asyncio.wait_for(consume(), 10)
        except asyncio.TimeoutError:
            pass

        assert kafka_output == messages

    loop.run_until_complete(test())
    loop.close()

    server.call("producer.close", [])


def test_producer_should_log_errors():
    server = get_server()

    server.call("producer.create", ["kafka:9090"])

    time.sleep(2)

    response = server.call("producer.get_errors", [])

    assert len(response) > 0
    assert len(response[0]) > 0

    server.call("producer.close", [])


def test_producer_stats():
    server = get_server()

    server.call("producer.create", ["kafka:9090"])

    time.sleep(2)

    response = server.call("producer.get_stats", [])
    assert len(response) > 0
    assert len(response[0]) > 0
    stat = json.loads(response[0][0])

    assert 'rdkafka#producer' in stat['name']
    assert 'kafka:9090/bootstrap' in stat['brokers']
    assert stat['type'] == 'producer'

    server.call("producer.close", [])


def test_producer_dump_conf():
    server = get_server()

    server.call("producer.create", ["kafka:9090"])

    time.sleep(2)

    response = server.call("producer.dump_conf", [])
    assert len(response) > 0
    assert len(response[0]) > 0
    assert 'session.timeout.ms' in response[0]
    assert 'socket.max.fails' in response[0]
    assert 'compression.codec' in response[0]

    server.call("producer.close", [])


def test_producer_metadata():
    server = get_server()

    server.call("producer.create", [KAFKA_HOST])

    time.sleep(2)

    response = server.call("producer.metadata", [])
    assert 'orig_broker_name' in response[0]
    assert 'orig_broker_id' in response[0]
    assert 'brokers' in response[0]
    assert 'topics' in response[0]
    assert 'host' in response[0]['brokers'][0]
    assert 'port' in response[0]['brokers'][0]
    assert 'id' in response[0]['brokers'][0]

    response = server.call("producer.list_groups", [])
    assert response[0] is not None
    response = server.call("producer.list_groups", [0])
    assert tuple(response) == (None, 'Local: Timed out')

    response = server.call("producer.metadata", [0])
    assert tuple(response) == (None, 'Local: Timed out')

    server.call("producer.close", [])

    server.call("producer.create", ["badhost:8080"])
    response = server.call("producer.metadata", [200])
    assert tuple(response) == (None, 'Local: Broker transport failure')
    response = server.call("producer.list_groups", [200])
    assert response[0] is None
    server.call("producer.close", [])


def test_producer_should_log_debug():
    server = get_server()

    server.call("producer.create", [KAFKA_HOST, {"debug": "broker,topic,msg"}])

    time.sleep(2)

    response = server.call("producer.get_logs", [])

    assert len(response) > 0
    assert len(response[0]) > 0

    server.call("producer.close", [])
