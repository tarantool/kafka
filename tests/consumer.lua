local box = require("box")
local json = require("json")
local log = require("log")
local os = require("os")
local fiber = require('fiber')
local tnt_kafka = require('kafka')

local consumer = nil
local errors = {}
local logs = {}
local rebalances = {}

local function create(brokers, options)
    local err
    errors = {}
    logs = {}
    rebalances = {}
    local error_callback = function(err)
        log.error("got error: %s", err)
        table.insert(errors, err)
    end
    local log_callback = function(fac, str, level)
        log.info("got log: %d - %s - %s", level, fac, str)
        table.insert(logs, string.format("got log: %d - %s - %s", level, fac, str))
    end

    local rebalance_callback = function(msg)
        log.info("got rebalance msg: %s", json.encode(msg))
        table.insert(rebalances, msg)
    end
    local default_options = {
        ["enable.auto.offset.store"] = "false",
        ["group.id"] = "test_consumer",
        ["auto.offset.reset"] = "earliest",
        ["enable.partition.eof"] = "false",
        ["log_level"] = "7",
    }
    for key, value in pairs(options or {}) do
        default_options[key] = value
    end
    consumer, err = tnt_kafka.Consumer.create({
        brokers = brokers,
        options = options,
        error_callback = error_callback,
        log_callback = log_callback,
        rebalance_callback = rebalance_callback,
        default_topic_options = {
            ["auto.offset.reset"] = "earliest",
        },
    })
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
    log.info("consumer created")
end

local function subscribe(topics)
    log.info("consumer subscribing")
    log.info(topics)
    local err = consumer:subscribe(topics)
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
    log.info("consumer subscribed")
end

local function unsubscribe(topics)
    log.info("consumer unsubscribing")
    log.info(topics)
    local err = consumer:unsubscribe(topics)
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
    log.info("consumer unsubscribed")
end

local function consume(timeout)
    log.info("consume called")

    local consumed = {}
    local f = fiber.create(function()
        local out = consumer:output()
        while true do
            if out:is_closed() then
                break
            end

            local msg = out:get()
            if msg ~= nil then
                log.info(msg)
                log.info("got msg with topic='%s' partition='%d' offset='%d' key='%s' value='%s'", msg:topic(), msg:partition(), msg:offset(), msg:key(), msg:value())
                table.insert(consumed, msg:value())
                local err = consumer:store_offset(msg)
                if err ~= nil then
                    log.error("got error '%s' while committing msg from topic '%s'", err, msg:topic())
                end
            end
        end
    end)

    log.info("consume wait")
    fiber.sleep(timeout)
    log.info("consume ends")

    f:cancel()

    return consumed
end

local function get_errors()
    return errors
end

local function get_logs()
    return logs
end

local function get_rebalances()
    return rebalances
end

local function close()
    log.info("closing consumer")
    local exists, err = consumer:close()
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
    log.info("consumer closed")
end

return {
    create = create,
    subscribe = subscribe,
    unsubscribe = unsubscribe,
    consume = consume,
    close = close,
    get_errors = get_errors,
    get_logs = get_logs,
    get_rebalances = get_rebalances,
}