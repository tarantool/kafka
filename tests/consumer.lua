local box = require("box")
local json = require("json")
local log = require("log")
local fiber = require('fiber')
local tnt_kafka = require('kafka')

local consumer = nil
local errors = {}
local logs = {}
local stats = {}
local rebalances = {}

local function create(brokers, additional_opts)
    local err
    errors = {}
    logs = {}
    stats = {}
    rebalances = {}
    local error_callback = function(err)
        log.error("got error: %s", err)
        table.insert(errors, err)
    end
    local log_callback = function(fac, str, level)
        log.info("got log: %d - %s - %s", level, fac, str)
        table.insert(logs, string.format("got log: %d - %s - %s", level, fac, str))
    end
    local stats_callback = function(json_stats)
        log.info("got stats")
        table.insert(stats, json_stats)
    end
    local rebalance_callback = function(msg)
        log.info("got rebalance msg: %s", json.encode(msg))
        table.insert(rebalances, msg)
    end

    local options = {
        ["enable.auto.offset.store"] = "false",
        ["group.id"] = "test_consumer",
        ["auto.offset.reset"] = "earliest",
        ["enable.partition.eof"] = "false",
        ["log_level"] = "7",
        ["statistics.interval.ms"] = "1000",
    }
    if additional_opts ~= nil then
        for key, value in pairs(additional_opts) do
            if value == nil then
                options[key] = nil
            else
                options[key] = value
            end
        end
    end
    consumer, err = tnt_kafka.Consumer.create({
        brokers = brokers,
        options = options,
        error_callback = error_callback,
        log_callback = log_callback,
        stats_callback = stats_callback,
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

local function msg_totable(msg)
    return {
        value = msg:value(),
        key = msg:key(),
        topic = msg:topic(),
        partition = msg:partition(),
        offset = msg:offset(),
        headers = msg:headers(),
    }
end

local function append_message(t, msg)
    table.insert(t, msg_totable(msg))
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
                log.info("%s", msg)
                log.info("got msg with topic='%s' partition='%d' offset='%d' key='%s' value='%s'", msg:topic(), msg:partition(), msg:offset(), msg:key(), msg:value())
                append_message(consumed, msg)
                local err = consumer:store_offset(msg)
                if err ~= nil then
                    log.error("got error '%s' while committing msg from topic '%s'", err, msg:topic())
                end
            else
                fiber.sleep(0.2)
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

local function get_stats()
    return stats
end

local function get_rebalances()
    return rebalances
end

local function dump_conf()
    return consumer:dump_conf()
end

local function metadata(timeout_ms)
    return consumer:metadata({timeout_ms = timeout_ms})
end

local function list_groups(timeout_ms)
    local res, err = consumer:list_groups({timeout_ms = timeout_ms})
    if err ~= nil then
        return nil, err
    end
    log.info("Groups: %s", json.encode(res))
    -- Some fields can have binary data that won't
    -- be correctly processed by connector.
    for _, group in ipairs(res) do 
        group['members'] = nil
    end
    return res
end

local function pause()
    return consumer:pause()
end

local function resume()
    return consumer:resume()
end

local function close()
    log.info("closing consumer")
    local _, err = consumer:close()
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
    log.info("consumer closed")
end

local function test_seek_partitions()
    log.info('Test seek')
    local messages = {}

    local out = consumer:output()

    for _ = 1, 5 do
        local msg = out:get(3)
        log.info('Get message: %s', json.encode(msg_totable(msg)))
        append_message(messages, msg)
        consumer:seek_partitions({
            {msg:topic(), msg:partition(), msg:offset()}
        }, 1000)
    end

    return messages
end

return {
    create = create,
    subscribe = subscribe,
    unsubscribe = unsubscribe,
    consume = consume,
    close = close,
    get_errors = get_errors,
    get_logs = get_logs,
    get_stats = get_stats,
    get_rebalances = get_rebalances,
    dump_conf = dump_conf,
    metadata = metadata,
    list_groups = list_groups,
    pause = pause,
    resume = resume,

    test_seek_partitions = test_seek_partitions,
}
