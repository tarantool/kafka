#!/usr/bin/env tarantool

local box = require('box')

box.cfg{
    listen = 3301
}

box.once('init', function()
    box.schema.user.grant("guest", 'read,write,execute,create,drop', 'universe')
end)

local cons, err = pcall(require, 'tests.consumer')
require('log').info(err)

rawset(_G, 'consumer', require('tests.consumer'))
rawset(_G, 'producer', require('tests.producer'))
