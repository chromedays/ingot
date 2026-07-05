cmake_minimum_required(VERSION 3.16)
project(pod_containers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(INGOT_BUILD_TESTS "Build ingot tests" OFF)

add_library(ingot ingot.cpp)
add_library(ingot::ingot ALIAS ingot)
target_include_directories(ingot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(INGOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
