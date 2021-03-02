local box = require('box')
local log = require('log')
local tnt_kafka = require('kafka')

local TOPIC_NAME = "test_producer"

local producer = nil
local errors = {}
local logs = {}

local function create(brokers, additional_opts)
    local err
    local options = {}
    errors = {}
    logs = {}
    local error_callback = function(err)
        log.error("got error: %s", err)
        table.insert(errors, err)
    end
    local log_callback = function(fac, str, level)
        log.info("got log: %d - %s - %s", level, fac, str)
        table.insert(logs, string.format("got log: %d - %s - %s", level, fac, str))
    end

    local options = {}
    if additional_opts ~= nil then
        for key, value in pairs(additional_opts) do
            options[key] = value
        end
    end

    producer, err = tnt_kafka.Producer.create({
        brokers = brokers,
        options = options,
        log_callback = log_callback,
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

local function init_transactions(timeout_ms)
    return producer:init_transactions(timeout_ms)
end

local function begin_transaction()
    return producer:begin_transaction()
end

local function commit_transaction(timeout_ms)
    return producer:commit_transaction(timeout_ms)
end

local function abort_transaction(timeout_ms)
    return producer:abort_transaction(timeout_ms)
end

local function get_errors()
    return errors
end

local function get_logs()
    return logs
end

local function close()
    local ok, err = producer:close()
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
    close = close,

    init_transactions = init_transactions,
    begin_transaction = begin_transaction,
    commit_transaction = commit_transaction,
    abort_transaction = abort_transaction,
}
