Tarantool kafka
===============
Full featured high performance kafka library for Tarantool based on [librdkafka](https://github.com/confluentinc/librdkafka).

Can produce more then 150k messages per second and consume more then 140k messages per second.

## Features
* Kafka producer and consumer implementations.
* Fiber friendly.
* Mostly errorless functions and methods. Error handling in Tarantool ecosystem is quite a mess, 
some libraries throw lua native `error` while others throws `box.error` instead. `kafka` returns 
non-critical errors as strings which allows you to decide how to handle it.

## Requirements 
* Tarantool >= 1.10.2
* Tarantool development headers 
* librdkafka >= 0.11.5
* librdkafka development headers
* openssl-libs
* openssl development headers
* make
* cmake
* gcc 

## Installation
```bash
    tt rocks install kafka
```

### Build module with statically linked librdkafka

To install the kafka module with builtin `librdkafka` dependency, use the `STATIC_BUILD` option:

```bash
tt rocks STATIC_BUILD=ON install kafka
```

Be aware that this approach doesn't include static openssl.
Instead, it assumes tarantool has openssl symbols exported.
That means, kafka static build is only usable with static tarantool build.

For a successful static build, you need to compile kafka
against the [same version of openssl](https://github.com/tarantool/tarantool/blob/800e5ed617f7cd352ec597ce16973c7e4cad76c8/static-build/CMakeLists.txt#L11) that tarantool does.

## Usage

Consumer
```lua
local os = require('os')
local log = require('log')
local tnt_kafka = require('kafka')

local consumer, err = tnt_kafka.Consumer.create({ brokers = "localhost:9092" })
if err ~= nil then
    print(err)
    os.exit(1)
end

local err = consumer:subscribe({ "some_topic" })
if err ~= nil then
    print(err)
    os.exit(1)
end

local out, err = consumer:output()
if err ~= nil then
    print(string.format("got fatal error '%s'", err))
    os.exit(1)
end

while true do
    if out:is_closed() then
        os.exit(1)
    end

    local msg = out:get()
    if msg ~= nil then
        print(string.format(
            "got msg with topic='%s' partition='%s' offset='%s' key='%s' value='%s'",
            msg:topic(), msg:partition(), msg:offset(), msg:key(), msg:value()
        ))
    end
end

-- from another fiber on app shutdown
consumer:close()
```

Producer
```lua
local os = require('os')
local log = require('log')
local tnt_kafka = require('kafka')

local producer, err = tnt_kafka.Producer.create({ brokers = "kafka:9092" })
if err ~= nil then
    print(err)
    os.exit(1)
end

for i = 1, 1000 do
    local message = "test_value " .. tostring(i)
    local err = producer:produce({
        topic = "test_topic",
        key = "test_key",
        value =  message
    })
    if err ~= nil then
        print(string.format("got error '%s' while sending value '%s'", err, message))
    else
        print(string.format("successfully sent value '%s'", message))
    end
end

producer:close()
```

You can pass additional configuration parameters for librdkafka 
https://github.com/confluentinc/librdkafka/blob/master/CONFIGURATION.md in special table `options` on client creation:
```lua
tnt_kafka.Producer.create({
    options = {
        ["some.key"] = "some_value",
    },
})

tnt_kafka.Consumer.create({
    options = {
        ["some.key"] = "some_value",
    },
})
```

More examples in `examples` folder.

## Using SSL

Connection to brokers using SSL supported by librdkafka itself so you only need to properly configure brokers by 
using this guide https://github.com/confluentinc/librdkafka/wiki/Using-SSL-with-librdkafka

After that you only need to pass following configuration parameters on client creation:
```lua
tnt_kafka.Producer.create({
    brokers = "broker_list",
    options = {
        ["security.protocol"] = "ssl",
        -- CA certificate file for verifying the broker's certificate.
        ["ssl.ca.location"] = "ca-cert",
        -- Client's certificate
        ["ssl.certificate.location"] = "client_?????_client.pem",
        -- Client's key
        ["ssl.key.location"] = "client_?????_client.key",
        -- Key password, if any
        ["ssl.key.password"] = "abcdefgh",
    },
})

tnt_kafka.Consumer.create({
    brokers = "broker_list",
    options = {
        ["security.protocol"] = "ssl",
        -- CA certificate file for verifying the broker's certificate.
        ["ssl.ca.location"] = "ca-cert",
        -- Client's certificate
        ["ssl.certificate.location"] = "client_?????_client.pem",
        -- Client's key
        ["ssl.key.location"] = "client_?????_client.key",
        -- Key password, if any
        ["ssl.key.password"] = "abcdefgh",
    },
})
```

## Known issues

## TODO
* Ordered storage for offsets to prevent commits unprocessed messages
* More examples
* Better documentation

## Benchmarks

Before any commands init and updated git submodule
```bash
    git submodule init
    git submodule update
```

### Producer

#### Async

Result: over 160000 produced messages per second on macbook pro 2016

Local run in docker:
```bash
    make docker-run-environment
    make docker-create-benchmark-async-producer-topic
    make docker-run-benchmark-async-producer-interactive
```

#### Sync

Result: over 90000 produced messages per second on macbook pro 2016

Local run in docker:
```bash
    make docker-run-environment
    make docker-create-benchmark-sync-producer-topic
    make docker-run-benchmark-sync-producer-interactive
```

### Consumer

#### Auto offset store enabled

Result: over 190000 consumed messages per second on macbook pro 2016

Local run in docker:
```bash
    make docker-run-environment
    make docker-create-benchmark-auto-offset-store-consumer-topic
    make docker-run-benchmark-auto-offset-store-consumer-interactive
```

#### Manual offset store

Result: over 190000 consumed messages per second on macbook pro 2016

Local run in docker:
```bash
    make docker-run-environment
    make docker-create-benchmark-manual-commit-consumer-topic
    make docker-run-benchmark-manual-commit-consumer-interactive
```

## Developing

### Tests
Before run any test you should add to `/etc/hosts` entry
```
127.0.0.1 kafka
```

You can run docker based integration tests via makefile target
```bash
    make test-run-with-docker
```
