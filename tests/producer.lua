local box = require('box')
local log = require('log')
local tnt_kafka = require('kafka')

local TOPIC_NAME = "test_producer"

local producer = nil
local errors = {}
local logs = {}
local stats = {}

local function create(brokers, additional_opts)
    local err
    errors = {}
    logs = {}
    stats = {}
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

    local options = {
        ["statistics.interval.ms"] = "1000",
    }
    if additional_opts ~= nil then
        for key, value in pairs(additional_opts) do
            options[key] = value
        end
    end

    producer, err = tnt_kafka.Producer.create({
        brokers = brokers,
        options = options,
        log_callback = log_callback,
        stats_callback = stats_callback,
        error_callback = error_callback,
        default_topic_options = {
            ["partitioner"] = "murmur2_random",
        },
    })
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
end

local function produce(messages)
    for _, message in ipairs(messages) do
        local err = producer:produce({topic = TOPIC_NAME, key = message, value = message})
        if err ~= nil then
            log.error("got error '%s' while sending value '%s'", err, message)
        else
            log.error("successfully sent value '%s'", message)
        end
    end
end

local function dump_conf()
    return producer:dump_conf()
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

local function metadata(timeout_ms, topic)
    return producer:metadata({timeout_ms = timeout_ms, topic = topic})
end

local function close()
    local _, err = producer:close()
    if err ~= nil then
        log.error("got err %s", err)
        box.error{code = 500, reason = err}
    end
end

return {
    create = create,
    produce = produce,
    get_errors = get_errors,
    get_logs = get_logs,
    get_stats = get_stats,
    close = close,
    dump_conf = dump_conf,
    metadata = metadata,
}
