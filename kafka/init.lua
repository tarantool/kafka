local log = require("log")
local fiber = require('fiber')
local tnt_kafka = require("kafka.tntkafka")

local DEFAULT_TIMEOUT_MS = 2000

local Consumer = {}

Consumer.__index = Consumer

function Consumer.create(config)
    if config == nil then
        return nil, "config must not be nil"
    end

    local consumer, err = tnt_kafka.create_consumer(config)
    if err ~= nil then
        return nil, err
    end

    local new = {
        config = config,
        _consumer = consumer,
        _output_ch = fiber.channel(10000),
    }
    setmetatable(new, Consumer)

    new._poll_msg_fiber = fiber.create(function()
        new:_poll_msg()
    end)

    if config.log_callback ~= nil then
        new._poll_logs_fiber = fiber.create(function()
            new:_poll_logs()
        end)
    end

    if config.stats_callback ~= nil then
        new._poll_stats_fiber = fiber.create(function()
            new:_poll_stats()
        end)
    end

    if config.error_callback ~= nil then
        new._poll_errors_fiber = fiber.create(function()
            new:_poll_errors()
        end)
    end

    if config.rebalance_callback ~= nil then
        new._poll_rebalances_fiber = fiber.create(function()
            new:_poll_rebalances()
        end)
    end

    return new, nil
end

function Consumer:_poll_msg()
    local msgs
    while true do
        msgs = self._consumer:poll_msg(100)
        if #msgs > 0 then
            for _, msg in ipairs(msgs) do
                self._output_ch:put(msg)
            end
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(0.01)
        end
    end
end

jit.off(Consumer._poll_msg)

function Consumer:_poll_logs()
    local count, err
    while true do
        count, err = self._consumer:poll_logs(100)
        if err ~= nil then
            log.error("Consumer poll logs error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Consumer._poll_logs)

function Consumer:_poll_stats()
    local count, err
    while true do
        count, err = self._consumer:poll_stats(100)
        if err ~= nil then
            log.error("Consumer poll stats error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Consumer._poll_stats)

function Consumer:_poll_errors()
    local count, err
    while true do
        count, err = self._consumer:poll_errors(100)
        if err ~= nil then
            log.error("Consumer poll errors error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Consumer._poll_errors)

function Consumer:_poll_rebalances()
    local count, err
    while true do
        count, err = self._consumer:poll_rebalances(1)
        if err ~= nil then
            log.error("Consumer poll rebalances error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(0.5)
        end
    end
end

jit.off(Consumer._poll_rebalances)

function Consumer:close()
    if self._consumer == nil then
        return false
    end

    local ok = self._consumer:close()

    self._poll_msg_fiber:cancel()
    self._output_ch:close()

    fiber.yield()

    if self._poll_logs_fiber ~= nil then
        self._poll_logs_fiber:cancel()
    end
    if self._poll_stats_fiber ~= nil then
        self._poll_stats_fiber:cancel()
    end
    if self._poll_errors_fiber ~= nil then
        self._poll_errors_fiber:cancel()
    end
    if self._poll_rebalances_fiber ~= nil then
        self._poll_rebalances_fiber:cancel()
    end

    self._consumer:destroy()

    self._consumer = nil

    return ok
end

function Consumer:subscribe(topics)
    return self._consumer:subscribe(topics)
end

function Consumer:unsubscribe(topics)
    return self._consumer:unsubscribe(topics)
end

function Consumer:output()
    return self._output_ch
end

function Consumer:store_offset(message)
    return self._consumer:store_offset(message)
end

function Consumer:dump_conf()
    if self._consumer == nil then
        return
    end
    return self._consumer:dump_conf()
end

function Consumer:metadata(options)
    if self._consumer == nil then
        return
    end

    local timeout_ms = DEFAULT_TIMEOUT_MS
    if options ~= nil and options.timeout_ms ~= nil then
        timeout_ms = options.timeout_ms
    end

    return self._consumer:metadata(timeout_ms)
end

local Producer = {}

Producer.__index = Producer

function Producer.create(config)
    if config == nil then
        return nil, "config must not be nil"
    end

    local producer, err = tnt_kafka.create_producer(config)
    if err ~= nil then
        return nil, err
    end

    local new = {
        config = config,
        _counter = 0,
        _delivery_map = {},
        _producer = producer,
    }
    setmetatable(new, Producer)

    new._msg_delivery_poll_fiber = fiber.create(function()
        new:_msg_delivery_poll()
    end)

    if config.log_callback ~= nil then
        new._poll_logs_fiber = fiber.create(function()
            new:_poll_logs()
        end)
    end

    if config.stats_callback ~= nil then
        new._poll_stats_fiber = fiber.create(function()
            new:_poll_stats()
        end)
    end

    if config.error_callback ~= nil then
        new._poll_errors_fiber = fiber.create(function()
            new:_poll_errors()
        end)
    end

    return new, nil
end

function Producer:_msg_delivery_poll()
    while true do
        local count, err
        while true do
            count, err = self._producer:msg_delivery_poll(100)
            if err ~= nil then
                log.error(err)
                -- throttling poll
                fiber.sleep(0.01)
            elseif count > 0 then
                fiber.yield()
            else
                -- throttling poll
                fiber.sleep(0.01)
            end
        end
    end
end

jit.off(Producer._msg_delivery_poll)

function Producer:_poll_logs()
    local count, err
    while true do
        count, err = self._producer:poll_logs(100)
        if err ~= nil then
            log.error("Producer poll logs error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Producer._poll_logs)

function Producer:_poll_stats()
    local count, err
    while true do
        count, err = self._producer:poll_stats(100)
        if err ~= nil then
            log.error("Producer poll stats error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Producer._poll_stats)

function Producer:_poll_errors()
    local count, err
    while true do
        count, err = self._producer:poll_errors(100)
        if err ~= nil then
            log.error("Producer poll errors error: %s", err)
            -- throttling poll
            fiber.sleep(0.1)
        elseif count > 0 then
            fiber.yield()
        else
            -- throttling poll
            fiber.sleep(1)
        end
    end
end

jit.off(Producer._poll_errors)

function Producer:produce_async(msg)
    local err = self._producer:produce(msg)
    return err
end

local function dr_callback_factory(delivery_chan)
    return function(err)
        delivery_chan:put(err)
    end
end

function Producer:produce(msg)
    local delivery_chan = fiber.channel(1)

    msg.dr_callback = dr_callback_factory(delivery_chan)

    local err = self._producer:produce(msg)
    if err == nil then
        err = delivery_chan:get()
    end

    return err
end

function Producer:dump_conf()
    if self._producer == nil then
        return
    end
    return self._producer:dump_conf()
end

function Producer:metadata(options)
    if self._producer == nil then
        return
    end

    local timeout_ms = DEFAULT_TIMEOUT_MS
    if options ~= nil and options.timeout_ms ~= nil then
        timeout_ms = options.timeout_ms
    end

    local topic
    if options ~= nil and options.topic ~= nil then
        topic = options.topic
    end

    return self._producer:metadata(topic, timeout_ms)
end

function Producer:close()
    if self._producer == nil then
        return false
    end

    local ok = self._producer:close()

    self._msg_delivery_poll_fiber:cancel()
    if self._poll_logs_fiber ~= nil then
        self._poll_logs_fiber:cancel()
    end
    if self._poll_stats_fiber ~= nil then
        self._poll_stats_fiber:cancel()
    end
    if self._poll_errors_fiber ~= nil then
        self._poll_errors_fiber:cancel()
    end

    self._producer:destroy()

    self._producer = nil

    return ok
end

return {
    Consumer = Consumer,
    Producer = Producer,
    _LIBRDKAFKA = tnt_kafka.librdkafka_version(),
}
