cmake_minimum_required(VERSION 3.10)
project(ART VERSION 1.0
                        DESCRIPTION "DISC fork of Adaptive Radix Tree"
                        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)


# Define build types
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")

include_directories(${CMAKE_SOURCE_DIR})

# Set compiler flags for different build types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
add_executable(main benchmarks/main.cpp)
add_executable(test_range benchmarks/test_range_query.cpp)
add_executable(profile_inserts benchmarks/profile_inserts.cpp)
add_executable(profile_inserts_with_file benchmarks/profile_inserts_with_file.cpp)
add_executable(art benchmarks/art.cpp)
add_executable(quart_tail benchmarks/quart_tail.cpp)
add_executable(quart_xtail benchmarks/quart_xtail.cpp)
add_executable(quart_lil benchmarks/quart_lil.cpp)