include_directories(${RDKAFKA_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

option(ENABLE_ASAN OFF)
message("ENABLE_ASAN = ${ENABLE_ASAN}")
if (ENABLE_ASAN)
    message("TESST")
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

add_library(tntkafka SHARED tnt_kafka.c callbacks.c consumer.c consumer_msg.c producer.c queue.c common.c)

if (APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} \
        -undefined suppress -flat_namespace")
endif(APPLE)

target_link_libraries(tntkafka pthread)

target_link_libraries(tntkafka ${RDKAFKA_LIBRARY})
set_target_properties(tntkafka PROPERTIES PREFIX "" OUTPUT_NAME "tntkafka")

install(TARGETS tntkafka LIBRARY DESTINATION ${TARANTOOL_INSTALL_LIBDIR}/kafka)
install(FILES init.lua DESTINATION ${TARANTOOL_INSTALL_LUADIR}/kafka)
