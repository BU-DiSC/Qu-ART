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
add_executable(main main.cpp)
add_executable(insert_profiling insert_profiling.cpp)
add_executable(run run.cpp)
add_executable(run_count_resets run_count_resets.cpp)
