import asyncio

from aiokafka import AIOKafkaConsumer
import tarantool

TIMEOUT_1S = 1000  # ms


def get_server():
    return tarantool.Connection("127.0.0.1", 3301,
                                user="guest",
                                password=None,
                                socket_timeout=10,
                                reconnect_max_attempts=3,
                                reconnect_delay=1,
                                connect_now=True)


async def check_result(loop, expected_result, timeout=5):
    kafka_output = []

    async def consume():
        consumer = AIOKafkaConsumer(
            'test_producer',
            group_id="test_group",
            loop=loop,
            bootstrap_servers='localhost:9092',
            auto_offset_reset="earliest",
            # Required to work with transactions
            isolation_level='read_committed',
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
        await asyncio.wait_for(consume(), timeout)
    except asyncio.TimeoutError:
        pass

    assert kafka_output == expected_result


def test_producer_dummy_transactions():
    server = get_server()

    server.call("producer.create", ["kafka:9092", {"transactional.id": "abcd"}])

    # Begin before init
    res = server.call("producer.begin_transaction")
    assert res[0] is False, res
    assert res[1] == 'Operation not valid in state Init'

    # Init transaction - OK
    res = server.call("producer.init_transactions", [TIMEOUT_1S])
    assert res[0] is True, res

    # Init transaction once again - not ok
    res = server.call("producer.init_transactions", [TIMEOUT_1S])
    assert res[0] is False, res
    assert res[1] == 'Operation not valid in state Ready'

    # Begin transaction - ok
    res = server.call("producer.begin_transaction")
    assert res[0] is True, res

    # Init transaction after begin - not ok
    res = server.call("producer.init_transactions", [TIMEOUT_1S])
    assert res[0] is False, res
    assert res[1] == 'Operation not valid in state InTransaction'

    # Begin transaction once again - not ok
    res = server.call("producer.begin_transaction")
    assert res[0] is False, res
    assert res[1] == 'Operation not valid in state InTransaction'

    # Commit transaction - ok
    res = server.call("producer.commit_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    # Commit transaction again - ok
    res = server.call("producer.commit_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    # Abort transaction is also idempotent
    res = server.call("producer.begin_transaction")
    assert res[0] is True, res

    res = server.call("producer.abort_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    res = server.call("producer.abort_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    loop = asyncio.get_event_loop()
    loop.run_until_complete(check_result(loop, []))

    server.call("producer.close", [])


def test_producer_committed_transaction():
    server = get_server()

    server.call("producer.create", ["kafka:9092", {"transactional.id": "abcd"}])

    # Init transaction - OK
    res = server.call("producer.init_transactions", [TIMEOUT_1S])
    assert res[0] is True, res

    res = server.call("producer.begin_transaction")
    assert res[0] is True, res

    server.call("producer.produce", (
        (
            "1",
            "2",
            "3",
        ),
    ))

    loop = asyncio.get_event_loop()

    loop.run_until_complete(check_result(loop, []))

    res = server.call("producer.commit_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    loop.run_until_complete(check_result(loop, [
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
    ]))

    server.call("producer.close", [])


def test_producer_aborted_transaction():
    server = get_server()

    server.call("producer.create", ["kafka:9092", {"transactional.id": "abcd"}])

    # Init transaction - OK
    res = server.call("producer.init_transactions", [TIMEOUT_1S])
    assert res[0] is True, res

    res = server.call("producer.begin_transaction")
    assert res[0] is True, res

    server.call("producer.produce", (
        (
            "1",
            "2",
            "3",
        ),
    ))

    loop = asyncio.get_event_loop()

    loop.run_until_complete(check_result(loop, []))

    res = server.call("producer.abort_transaction", [TIMEOUT_1S])
    assert res[0] is True, res

    loop.run_until_complete(check_result(loop, []))

    server.call("producer.close", [])
