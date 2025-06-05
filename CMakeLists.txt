cmake_minimum_required(VERSION 3.10)
project(ART VERSION 1.0
                        DESCRIPTION "DISC fork of Adaptive Radix Tree"
                        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)


# Define build types
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")

# Set compiler flags for different build types
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
add_executable(main main.cpp)
