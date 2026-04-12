cmake_minimum_required(VERSION 3.10)
project(QuIT_runner CXX)

set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# quick-insertion-tree is cloned to this directory by experiments/setup.sh
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(run_quit_2k run_quit.cpp)
target_compile_definitions(run_quit_2k PRIVATE
    LOL_FAT VARIABLE_SPLIT REDISTRIBUTE LOL_RESET INMEMORY BLOCK_SIZE_BYTES=2048 QUIT_FAT)
target_include_directories(run_quit_2k PRIVATE "${REPO_ROOT}")

# Plain B+-tree baseline (no QuIT fast-path features) — used by experiment 5.3
add_executable(run_quit_base_2k run_quit.cpp)
target_compile_definitions(run_quit_base_2k PRIVATE INMEMORY BLOCK_SIZE_BYTES=2048)
target_include_directories(run_quit_base_2k PRIVATE "${REPO_ROOT}")
