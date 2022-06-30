cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(kafka C)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

# Set CFLAGS
set(MY_C_FLAGS "-Wall -Wextra -Werror -std=gnu11 -fno-strict-aliasing -Wno-deprecated-declarations")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MY_C_FLAGS}")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${MY_C_FLAGS} -ggdb3")

find_package(Tarantool REQUIRED)

set(STATIC_BUILD "OFF" CACHE BOOL "Link dependencies statically?")
set(WITH_OPENSSL_1_1 "OFF" CACHE BOOL "Require openssl version >= 1.1?")

if (WITH_OPENSSL_1_1)
    find_package(OpenSSL 1.1 REQUIRED)
else()
    find_package(OpenSSL REQUIRED)
endif()
message("Found OPENSSL version: ${OPENSSL_VERSION}")

option(ENABLE_ASAN OFF)
if (ENABLE_ASAN)
    set(LIBRDKAFKA_C_FLAGS "-fsanitize=address")
    set(LIBRDKAFKA_CXX_FLAGS "-fsanitize=address")
    set(LIBRDKAFKA_FLAGS "--enable-devel")
endif()

if(STATIC_BUILD)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/librdkafka/lib/librdkafka.a
        COMMAND ./configure --enable-ssl
                            --disable-zstd
                            --prefix=${CMAKE_BINARY_DIR}/librdkafka
                            ${LIBRDKAFKA_FLAGS}
                            --cc=${CMAKE_C_COMPILER}
                            --cxx=${CMAKE_CXX_COMPILER}
                            --CFLAGS=${LIBRDKAFKA_C_FLAGS}
                            --CXXFLAGS=${LIBRDKAFKA_CXX_FLAGS}
        COMMAND $(MAKE) -j
        COMMAND $(MAKE) install
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/librdkafka
        COMMENT "Building librdkafka"
        VERBATIM
    )
    add_custom_target(librdkafka.a ALL
        DEPENDS ${CMAKE_BINARY_DIR}/librdkafka/lib/librdkafka.a
    )

    add_library(librdkafka_static INTERFACE)
    target_include_directories(librdkafka_static INTERFACE ${CMAKE_BINARY_DIR}/librdkafka/include)
    target_link_libraries(librdkafka_static INTERFACE ${CMAKE_BINARY_DIR}/librdkafka/lib/librdkafka.a)
    add_dependencies(librdkafka_static librdkafka.a)

    set(RDKAFKA_LIBRARY ${RDKAFKA_LIBRARY} librdkafka_static)
else()
    find_package(RDKAFKA REQUIRED)
    # Link RdKafka transitive dependencies manually
    set(RDKAFKA_LIBRARY ${RDKAFKA_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})
endif()

include_directories(${TARANTOOL_INCLUDE_DIRS})

add_subdirectory(kafka)
