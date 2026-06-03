cmake_minimum_required(VERSION 3.10)

project(ART VERSION 1.0
        DESCRIPTION "DISC fork of Adaptive Radix Tree"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release only if no build type is set (and not a multi-config generator)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

# Allow selection in cache GUIs / command line
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

# ── Key-width selection ────────────────────────────────────────────────────────
# Set QUART_KEY_64=ON to build with 64-bit integer keys (default: 32-bit).
#   cmake -DQUART_KEY_64=ON ..
option(QUART_KEY_64 "Use 64-bit integer keys (default: 32-bit)" OFF)
if(QUART_KEY_64)
    message(STATUS "QuART: building with 64-bit keys")
    add_compile_definitions(QUART_KEY_64)
else()
    message(STATUS "QuART: building with 32-bit keys (default)")
endif()

# ── k-fp classification counters ────────────────────────────────────────────────
# Set QUART_KFP_STATS=ON to compile the k-fp classification counters
# (FP_INSERT / BRIDGE / NO_MATCH tallies, fp-type histograms) into test_kfp.
# These add an increment to the QuART_kfp insert hot path, so they are OFF by
# default to keep timing runs clean.  The `run` benchmark never compiles them in.
#   cmake -DQUART_KFP_STATS=ON ..
option(QUART_KFP_STATS "Compile k-fp classification counters into test_kfp" OFF)
if(QUART_KFP_STATS)
    message(STATUS "QuART: k-fp stat counters ENABLED (test_kfp)")
else()
    message(STATUS "QuART: k-fp stat counters disabled (default)")
endif()

# ── Targets ─────────────────────────────────────────────────────────────────────
# All executables are tuned for the build host with -march=native.  This enables
# the wider instruction set the QuART_kfp Parallel findSlot and the Node16 SSE
# compares benefit from.  Note: it applies to the WHOLE binary, so every tree type
# is built for this CPU and the result may SIGILL on an older/different machine.
#
# Note: QuArtNodeMethods.cpp / QuArtNodeBulkLoadMethods.cpp are #include'd from the
# QuART headers, so they must NOT be listed as separate translation units here.

# Main benchmark driver
add_executable(run run.cpp)

# k-FP correctness tests / benchmark
add_executable(test_kfp test_kfp.cpp)
if(QUART_KFP_STATS)
    target_compile_definitions(test_kfp PRIVATE QUART_KFP_STATS)
endif()

# TPC-C insert experiment (ART paper replication, warehouse=1)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tpcc/tpcc_art.cpp")
    add_executable(tpcc_art tpcc/tpcc_art.cpp)
endif()

foreach(target run test_kfp tpcc_art)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE -march=native)
    endif()
endforeach()
