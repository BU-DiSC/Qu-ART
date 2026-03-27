cmake_minimum_required(VERSION 3.10)

project(ART VERSION 1.0
        DESCRIPTION "DISC fork of Adaptive Radix Tree"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

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

# Targets
# add_executable(main main.cpp)
# add_executable(insert_profiling insert_profiling.cpp)
add_executable(run run.cpp)
add_executable(test_compress test/test_compress.cpp)

# QuIT variants — two leaf sizes are built for comparison:
#   256-key leaves: BLOCK_SIZE_BYTES=2060  (256*8+12)
#   512-key leaves: BLOCK_SIZE_BYTES=4108  (512*8+12)
# Each target is a separate binary because the tree variant is selected at compile time.
add_executable(run_quit_simple run_quit.cpp)
target_compile_definitions(run_quit_simple PRIVATE INMEMORY BLOCK_SIZE_BYTES=2060)

add_executable(run_quit_tail run_quit.cpp)
target_compile_definitions(run_quit_tail PRIVATE TAIL_FAT INMEMORY BLOCK_SIZE_BYTES=2060)

add_executable(run_quit_lil run_quit.cpp)
target_compile_definitions(run_quit_lil PRIVATE LIL_FAT INMEMORY BLOCK_SIZE_BYTES=2060)

add_executable(run_quit_lol run_quit.cpp)
target_compile_definitions(run_quit_lol PRIVATE LOL_FAT INMEMORY BLOCK_SIZE_BYTES=2060)

# Full QuIT — 2k bytes
add_executable(run_quit run_quit.cpp)
target_compile_definitions(run_quit PRIVATE LOL_FAT VARIABLE_SPLIT REDISTRIBUTE LOL_RESET INMEMORY BLOCK_SIZE_BYTES=2056)

# Full QuIT — 512-key leaves
add_executable(run_quit_512 run_quit.cpp)
target_compile_definitions(run_quit_512 PRIVATE LOL_FAT VARIABLE_SPLIT REDISTRIBUTE LOL_RESET INMEMORY BLOCK_SIZE_BYTES=4096)
