cmake_minimum_required(VERSION 3.22)
project(containers VERSION 0.1)
enable_testing()
include(FetchContent)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
include(cmake/clang-format.cmake)
if(WIN32)
    include(cmake/group-sources.cmake)
endif()
include(cmake/md.cmake)
include(cmake/sanitizers.cmake)

sanitizers_init(CONTAINERS)

# TODO: g++ -Wall -Wextra -Wpedantic -Q --help=warning | grep disabled
set(CONTAINERS_WARNINGS_GCC
    -Wall
    # -Wpedantic - VLA support
    -Wextra 
    -Wshadow
    -Wno-unused-variable 
    -Wno-gnu-zero-variadic-macro-arguments
)

set(CONTAINERS_WARNINGS_MSVC
    /W4
    /wd4101 # unreferenced local variable
    /wd4702 # unreachable code
)

option(CONTAINERS_ENABLE_TESTING "enable testing through googletest" ON)
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

    if(MSVC)
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
    include/containers/allocators/arena_allocator.h
    include/containers/allocators/page_allocator.h
    include/containers/growable_array.h
    include/containers/evictable_unordered_map.h
    README.md
)

if(CONTAINERS_ENABLE_BENCHMARK)
    FetchContent_Declare(concurrentqueue
        GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
        GIT_TAG master) 
    FetchContent_MakeAvailable(concurrentqueue)

    add_executable(containers_benchmark
        benchmark/arena_allocator.cpp
        benchmark/evictable_unordered_map.cpp
        benchmark/growable_array.cpp
        benchmark/main.cpp
    )

    target_link_libraries(containers_benchmark 
        containers 
        benchmark::benchmark
        concurrentqueue
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(containers_benchmark PRIVATE benchmark)
    if(MSVC)
        target_compile_definitions(containers_benchmark PRIVATE -D_ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()

    target_compile_options(containers_benchmark PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:${CONTAINERS_WARNINGS_GCC}>)
    target_compile_options(containers_benchmark PRIVATE $<$<CXX_COMPILER_ID:MSVC>:${CONTAINERS_WARNINGS_MSVC} /bigobj>)
endif()

if(CONTAINERS_ENABLE_TESTING)
    add_executable(containers_test
        test/arena_allocator.cpp
        test/evictable_unordered_map.cpp
        test/growable_array.cpp
    )

    add_test(NAME containers_test COMMAND containers_test)
    target_link_libraries(containers_test 
        containers	
        gtest_main
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(containers_test PRIVATE test)
    if(MSVC)
        target_compile_definitions(containers_test PRIVATE -D_ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()

    target_compile_options(containers_test PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:${CONTAINERS_WARNINGS_GCC}>)
    target_compile_options(containers_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:${CONTAINERS_WARNINGS_MSVC} /bigobj>)

    target_sanitize(CONTAINERS containers_test)
endif()
