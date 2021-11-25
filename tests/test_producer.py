import os
import time
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

    server.call("producer.produce", (
        (
            "1",
            "2",
            "3",
        ),
    ))

    loop = asyncio.get_event_loop()

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
                    kafka_output.append({
                        'key': msg.key if msg.key is None else msg.key.decode('utf8'),
                        'value': msg.value if msg.value is None else msg.value.decode('utf8')
                    })

            finally:
                # Will leave consumer group; perform autocommit if enabled.
                await consumer.stop()

        try:
            await asyncio.wait_for(consume(), 10)
        except asyncio.TimeoutError:
            pass

        assert kafka_output == [
            {
                "key": "1",
                "value": "1"
            },
            {
                "key": "2",
                "value": "2"
            },
            {
                "key": "3",
                "value": "3"
            },
        ]

    loop.run_until_complete(test())

    server.call("producer.close", [])


def test_producer_should_log_errors():
    server = get_server()

    server.call("producer.create", ["kafka:9090"])

    time.sleep(2)

    response = server.call("producer.get_errors", [])

    assert len(response) > 0
    assert len(response[0]) > 0

    server.call("producer.close", [])


def test_producer_should_log_debug():
    server = get_server()

    server.call("producer.create", [KAFKA_HOST, {"debug": "broker,topic,msg"}])

    time.sleep(2)

    response = server.call("producer.get_logs", [])

    assert len(response) > 0
    assert len(response[0]) > 0

    server.call("producer.close", [])
