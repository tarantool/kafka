local ffi = require('ffi')
local fiber = require('fiber')

ffi.cdef[[
    int rd_kafka_test_fatal_error(void *rk, int err, const char *reason);
]]

-- Tarantool also searches local .rocks directories outside package.cpath.
local native_path = assert(package.search('kafka.tntkafka'),
                           'Cannot find kafka.tntkafka for fatal error injection')
local native = ffi.load(native_path)
local FATAL = -150 -- RD_KAFKA_RESP_ERR__FATAL
local FAILURE = -196 -- RD_KAFKA_RESP_ERR__FAIL

local function inject(handle, reason)
    local rk = ffi.cast('void ***', handle)[0][0]
    assert(native.rd_kafka_test_fatal_error(rk, FAILURE, reason) == 0)
end

local function wait_for(predicate)
    local deadline = fiber.time() + 10
    while not predicate() and fiber.time() < deadline do
        fiber.sleep(0.01)
    end
    assert(predicate(), 'Timed out waiting for error callback')
end

local function test_fatal_error(kind, class, with_callback)
    local events = {}
    local config = {
        brokers = '127.0.0.1:1',
        options = {['group.id'] = 'fatal-error-test'},
    }
    if with_callback then
        config.error_callback = function(...)
            table.insert(events, {...})
        end
    end
    local handle, err = class.create(config)
    assert(handle, err)
    local raw = handle['_' .. kind]
    assert(handle:fatal_error() == nil)
    assert(raw:fatal_error() == nil)

    if with_callback then
        wait_for(function() return #events > 0 end)
        for _, event in ipairs(events) do
            assert(#event == 2)
            assert(type(event[1]) == 'string')
            assert(type(event[2]) == 'number')
            assert(event[2] ~= FATAL)
        end
        assert(handle:fatal_error() == nil)
    end

    local reason = kind .. ' injected fatal error'
    inject(raw, reason)
    local fatal = handle:fatal_error()
    assert(type(fatal) == 'string' and fatal:find(reason, 1, true))
    assert(handle:fatal_error() == fatal)
    assert(raw:fatal_error() == fatal)

    if with_callback then
        local function received_fatal()
            for _, event in ipairs(events) do
                if event[2] == FATAL then
                    return true
                end
            end
            return false
        end
        wait_for(received_fatal)
        for _, event in ipairs(events) do
            assert(#event == 2)
            assert(type(event[2]) == 'number')
            assert(type(event[1]) == 'string')
        end
    end

    assert(handle:close())
    assert(handle:fatal_error() == nil)
    assert(raw:fatal_error() == nil)
end

return {test_fatal_error = test_fatal_error}
