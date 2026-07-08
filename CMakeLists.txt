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

# ── multi-fp classification counters ────────────────────────────────────────────────
# Set QUART_MULTI_FP_STATS=ON to compile the multi-fp classification counters
# (FP_INSERT / BRIDGE / NO_MATCH tallies, fp-type histograms) into test_multi_fp.
# These add an increment to the QuART_multi_fp insert hot path, so they are OFF by
# default to keep timing runs clean.  The `run` benchmark never compiles them in.
#   cmake -DQUART_MULTI_FP_STATS=ON ..
option(QUART_MULTI_FP_STATS "Compile multi-fp classification counters into test_multi_fp" OFF)
if(QUART_MULTI_FP_STATS)
    message(STATUS "QuART: multi-fp stat counters ENABLED (test_multi_fp)")
else()
    message(STATUS "QuART: multi-fp stat counters disabled (default)")
endif()

# Set QUART_CONC_STATS=ON to compile lock-contention / restart counters into
# test_conc_olc: how many times a descender had to restart, why, and how long it
# waited on a lock.  These add atomic increments to the insert hot path, so they
# are OFF by default to keep timing runs clean.
#   cmake -DQUART_CONC_STATS=ON ..
option(QUART_CONC_STATS "Compile lock-contention counters into test_conc_olc" OFF)
if(QUART_CONC_STATS)
    message(STATUS "QuART: conc lock counters ENABLED (test_conc_olc)")
else()
    message(STATUS "QuART: conc lock counters disabled (default)")
endif()

# Set QUART_STAIL_FP_STATS=ON to compile QuART_conc_olc_stail's fast-path
# effectiveness counters into test_conc_olc: the per-key classification
# histogram (FP_INSERT@leaf / FP_INSERT@sub-leaf / BRIDGE / OTHER-change /
# OTHER-preserve / warmup), the fast-append hit rate (tryFastAppend attempts vs
# successes), and the fast-append contention rate (attempts whose tail-node
# write-lock was contended).  Like the other stat flags these add atomic
# increments to the insert hot path, so they are OFF by default to keep timing
# runs clean; enable them for a separate counts-only build.
#   cmake -DQUART_STAIL_FP_STATS=ON ..
option(QUART_STAIL_FP_STATS "Compile stail fast-path effectiveness counters into test_conc_olc" OFF)
if(QUART_STAIL_FP_STATS)
    message(STATUS "QuART: stail fast-path counters ENABLED (test_conc_olc)")
else()
    message(STATUS "QuART: stail fast-path counters disabled (default)")
endif()

# ── Targets ─────────────────────────────────────────────────────────────────────
# All executables are tuned for the build host with -march=native.  This enables
# the wider instruction set the QuART_multi_fp Branchless findSlot and the Node16 SSE
# compares benefit from.  Note: it applies to the WHOLE binary, so every tree type
# is built for this CPU and the result may SIGILL on an older/different machine.
#
# Note: QuArtNodeMethods.cpp / QuArtNodeBulkLoadMethods.cpp are #include'd from the
# QuART headers, so they must NOT be listed as separate translation units here.

# Main benchmark driver
add_executable(run run.cpp)

# multi-FP correctness tests / benchmark
add_executable(test_multi_fp test_multi_fp.cpp)
if(QUART_MULTI_FP_STATS)
    target_compile_definitions(test_multi_fp PRIVATE QUART_MULTI_FP_STATS)
endif()

# multi-FP slot-count (K) sweep: 1,2,4,8,16,32,64 (superseded by test_multi_fp_grid,
# which sweeps K for each stream count W on the real bods workloads; kept
# optional so an old checkout that still has the source builds it).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multi_fp_sweep.cpp")
    add_executable(test_multi_fp_sweep test_multi_fp_sweep.cpp)
endif()

# multi-FP 2-D grid: slot-count (K) × number of interleaved real bods streams (W)
add_executable(test_multi_fp_grid test_multi_fp_grid.cpp)

# test_conc_olc: robust, PARAMETER-FREE concurrent QuART with an Optimistic-Lock-
# Coupling slow path (lock-free versioned reads on descent).  No fork-depth, no
# pre-warm; parallelism follows the tree's actual shape, so streams that share a
# byte, don't, or fully overlap are all handled by the same code.  A/Bs two tree
# types: QuART_conc_olc_art (no fast path) vs QuART_conc_olc_stail (per-thread
# stail fast-path tail), both built on the shared base in trees/olc_base.h.
find_package(Threads REQUIRED)
add_executable(test_conc_olc test_conc_olc.cpp)
target_link_libraries(test_conc_olc PRIVATE Threads::Threads)
if(QUART_CONC_STATS)
    target_compile_definitions(test_conc_olc PRIVATE QUART_CONC_STATS)
endif()
if(QUART_STAIL_FP_STATS)
    target_compile_definitions(test_conc_olc PRIVATE QUART_STAIL_FP_STATS)
endif()

# QuART_conc_olc_single / test_conc_olc_single: single-STREAM OLC A/B benchmark.
# Two OLC ARTs that share identical descent machinery and differ ONLY in the
# fast path: QuART_conc_olc_art (none) vs QuART_conc_olc_stail (one globally
# shared fast-path tail).  Isolates the value of a single shared fp for ONE
# near-sorted stream inserted by many threads.  Shared OLC version-word
# primitives live in trees/olc_common.h.
add_executable(test_conc_olc_single test_conc_olc_single.cpp)
target_link_libraries(test_conc_olc_single PRIVATE Threads::Threads)

# TPC-C insert experiment (ART paper replication, warehouse=1)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tpcc/tpcc_art.cpp")
    add_executable(tpcc_art tpcc/tpcc_art.cpp)
endif()

foreach(target run test_multi_fp test_multi_fp_grid test_conc_olc test_conc_olc_single tpcc_art)
    if(TARGET ${target})
        target_compile_options(${target} PRIVATE -march=native)
    endif()
endforeach()
