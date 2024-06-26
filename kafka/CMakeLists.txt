include_directories(${RDKAFKA_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(tntkafka SHARED tnt_kafka.c callbacks.c consumer.c consumer_msg.c producer.c queue.c common.c)

if (SANITIZER_FLAGS)
    separate_arguments(SANITIZER_FLAGS UNIX_COMMAND "${SANITIZER_FLAGS}")
    target_compile_options(tntkafka PRIVATE ${SANITIZER_FLAGS})
    target_link_options(tntkafka PRIVATE ${SANITIZER_FLAGS})
endif()

if (APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} \
        -undefined suppress -flat_namespace")
endif(APPLE)

find_package(Threads REQUIRED)
target_link_libraries(tntkafka ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(tntkafka PROPERTIES C_VISIBILITY_PRESET hidden)

target_link_libraries(tntkafka ${RDKAFKA_LIBRARY})
set_target_properties(tntkafka PROPERTIES PREFIX "" OUTPUT_NAME "tntkafka")

install(TARGETS tntkafka LIBRARY DESTINATION ${TARANTOOL_INSTALL_LIBDIR}/kafka)
install(FILES init.lua version.lua DESTINATION ${TARANTOOL_INSTALL_LUADIR}/kafka)
