cmake_minimum_required(VERSION 3.22)
project(containers VERSION 0.1)
include(FetchContent)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS NO)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
enable_testing()
option(CONTAINERS_ENABLE_TESTING "enable testing through googletest" OFF)
option(CONTAINERS_ENABLE_BENCHMARK "enable benchmarking through googlebenchmark" ON)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache) 
endif(CCACHE_FOUND)

if(CONTAINERS_ENABLE_TESTING)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG main)

    if(WIN32)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(googletest)
endif()

if(CONTAINERS_ENABLE_BENCHMARK)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main) 
       
    set(BENCHMARK_ENABLE_TESTING off)
    FetchContent_MakeAvailable(benchmark)
endif()

add_library(containers INTERFACE)
target_include_directories(containers INTERFACE include)

target_sources(containers INTERFACE
    include/containers/lockfree/queue.h
    include/containers/lockfree/stack.h
    include/containers/lockfree/detail/hazard_era_allocator.h
    README.md
)

if(CONTAINERS_ENABLE_BENCHMARK)
    add_executable(containers_benchmark
        benchmark/main.cpp
        benchmark/lockfree/stack.cpp
        benchmark/lockfree/queue.cpp
    )

    target_link_libraries(containers_benchmark containers benchmark::benchmark)
    target_include_directories(containers_benchmark PRIVATE benchmark)
endif()
