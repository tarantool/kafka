import time

from kafka import KafkaConsumer
import tarantool


def get_server():
    return tarantool.Connection("127.0.0.1", 3301,
                                user="guest",
                                password=None,
                                socket_timeout=10,
                                reconnect_max_attempts=3,
                                reconnect_delay=1,
                                connect_now=True)


def wait(fn, *args, timeout=10):
    end = time.time() + timeout
    resp = None
    while time.time() < end:
        try:
            resp = fn(*args)
            break
        except Exception:
            time.sleep(0.1)

    return resp


def test_producer_should_produce_msgs():
    consumer = KafkaConsumer(
        'test_producer',
        group_id="test_group",
        auto_offset_reset="latest",
    )

    server = get_server()
    server.call("producer.create", ["kafka:9092"])
    server.call("producer.produce", (
        (
            "1",
            "2",
            "3",
        ),
    ))

    def gather_messages(messages):
        response = consumer.poll(500)
        result = list(response.values()).pop()
        for m in result:
            messages.append({
                'key':   m.key if m.key is None else m.key.decode('utf8'),
                'value': m.value if m.value is None else m.value.decode('utf8')
            })
        if len(messages) < 3:
            raise RuntimeError("Not enough messages yet")
        return messages

    kafka_output = wait(gather_messages, [])

    consumer.commit()

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

    server.call("producer.create", ["kafka:9092", {"debug": "broker,topic,msg"}])

    time.sleep(2)

    response = server.call("producer.get_logs", [])

    assert len(response) > 0
    assert len(response[0]) > 0

    server.call("producer.close", [])
