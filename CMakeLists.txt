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

if (APPLE)
    set(LIBRDKAFKA_CXX_FLAGS "${LIBRDKAFKA_CXX_FLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    set(LIBRDKAFKA_C_FLAGS "${LIBRDKAFKA_C_FLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
endif()

if(STATIC_BUILD)
    include(ExternalProject)
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/patches")
    ExternalProject_Add(librdkafka
        SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/librdkafka
        INSTALL_DIR ${CMAKE_BINARY_DIR}/librdkafka
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND <SOURCE_DIR>/configure
            --cc=${CMAKE_C_COMPILER}
            --cxx=${CMAKE_CXX_COMPILER}
            --CFLAGS=${LIBRDKAFKA_C_FLAGS}
            --CPPFLAGS=${LIBRDKAFKA_CXX_FLAGS}
            --prefix=<INSTALL_DIR>
            ${LIBRDKAFKA_FLAGS}

            --enable-ssl
            --disable-zstd
            --disable-lz4
            --disable-lz4-ext
            --enable-static
        BUILD_COMMAND make -C src -j
        INSTALL_COMMAND make -C src install
        PATCH_COMMAND patch -d <SOURCE_DIR> -p1 -i "${PATCHES_DIR}/librdkafka-tarantool-security-47.patch"
        COMMAND       patch -d <SOURCE_DIR> -p1 -i "${PATCHES_DIR}/librdkafka-tarantool-security-52.patch"
        COMMAND       patch -d <SOURCE_DIR> -p1 -i "${PATCHES_DIR}/librdkafka-tarantool-security-55.patch"
        COMMAND       patch -d <SOURCE_DIR> -p1 -i "${PATCHES_DIR}/librdkafka-tarantool-security-70.patch"
        COMMAND       patch -d <SOURCE_DIR> -p1 -i "${PATCHES_DIR}/librdkafka-tarantool-security-36.patch"
    )

    add_library(librdkafka_static INTERFACE)
    add_dependencies(librdkafka_static librdkafka)
    ExternalProject_Get_Property(librdkafka INSTALL_DIR)
    target_include_directories(librdkafka_static INTERFACE ${INSTALL_DIR}/include)
    target_link_libraries(librdkafka_static INTERFACE ${INSTALL_DIR}/lib/librdkafka.a)

    set(RDKAFKA_LIBRARY ${RDKAFKA_LIBRARY} librdkafka_static)
else()
    find_package(RDKAFKA REQUIRED)
    # Link RdKafka transitive dependencies manually
    set(RDKAFKA_LIBRARY ${RDKAFKA_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})
endif()

include_directories(${TARANTOOL_INCLUDE_DIRS})

add_subdirectory(kafka)
