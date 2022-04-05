CMAKE_MINIMUM_REQUIRED (VERSION 3.0.2)

INCLUDE (cmake/cpp17.cmake)
INCLUDE (cmake/subdirs.cmake)
INCLUDE (cmake/openssl.cmake)

OPTION (BUILD_BENCHMARK "Build benchmark" OFF)
OPTION (BUILD_TESTS "Build tests" OFF)
OPTION (WITH_OPENSSL "Use OpenSSL for TLS connections" OFF)
OPTION (USE_SYSTEM_ABSEIL "Use system ABSEIL" OFF)
OPTION (USE_SYSTEM_LZ4 "Use system LZ4" OFF)
OPTION (USE_SYSTEM_CITYHASH "Use contrib cityhash" OFF)

PROJECT (CLICKHOUSE-CLIENT)

    USE_CXX17 ()
    USE_OPENSSL ()

    IF (NOT CMAKE_BUILD_TYPE)
        SET (CMAKE_BUILD_TYPE "RelWithDebInfo")
    ENDIF ()

    IF (UNIX)
        IF (NOT APPLE)
            SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
        ENDIF ()
        SET (CMAKE_EXE_LINKER_FLAGS, "${CMAKE_EXE_LINKER_FLAGS} -lpthread")
        # -Wpedantic makes int128 support somewhat harder and less performant (by not allowing builtin __int128)
        SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
    ENDIF ()

    INCLUDE_DIRECTORIES (.)

    IF (USE_SYSTEM_ABSEIL)
        FIND_PACKAGE(absl REQUIRED)
    ELSE ()
        INCLUDE_DIRECTORIES (contrib/absl)
        SUBDIRS (contrib/absl/absl)
    ENDIF ()

    IF (USE_SYSTEM_LZ4)
        FIND_PACKAGE(lz4 REQUIRED)
    ELSE ()
        INCLUDE_DIRECTORIES (contrib/lz4/lz4)
        SUBDIRS (contrib/lz4/lz4)
    ENDIF ()

    IF (USE_SYSTEM_CITYHASH)
        FIND_PACKAGE(cityhash REQUIRED)
    ELSE ()
        INCLUDE_DIRECTORIES (contrib/cityhash/cityhash)
        SUBDIRS (contrib/cityhash/cityhash)
    ENDIF ()

    SUBDIRS (
        clickhouse
    )

    IF (BUILD_BENCHMARK)
        SUBDIRS (bench)
    ENDIF (BUILD_BENCHMARK)

    IF (BUILD_TESTS)
        INCLUDE_DIRECTORIES (contrib/gtest/include contrib/gtest)
        SUBDIRS (
            contrib/gtest
            tests/simple
            ut
        )
    ENDIF (BUILD_TESTS)
