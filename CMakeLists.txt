cmake_minimum_required(VERSION 3.22)
project(elasticheap VERSION 0.1)
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

sanitizers_init(ELASTICHEAP)

# TODO: g++ -Wall -Wextra -Wpedantic -Q --help=warning | grep disabled
set(ELASTICHEAP_WARNINGS_GCC
    -Wall
    # -Wpedantic - VLA support
    -Wextra 
    -Wshadow
    -Wno-unused-variable 
    -Wno-gnu-zero-variadic-macro-arguments
)

set(ELASTICHEAP_WARNINGS_MSVC
    /W4
    /wd4101 # unreferenced local variable
    /wd4702 # unreachable code
)

option(ELASTICHEAP_ENABLE_TESTING "enable testing through googletest" ON)
option(ELASTICHEAP_ENABLE_BENCHMARK "enable benchmarking through googlebenchmark" ON)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache) 
endif(CCACHE_FOUND)

if(ELASTICHEAP_ENABLE_TESTING)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG main)

    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(googletest)
endif()

if(ELASTICHEAP_ENABLE_BENCHMARK)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG main) 
       
    set(BENCHMARK_ENABLE_TESTING off)
    FetchContent_MakeAvailable(benchmark)
endif()

add_library(elasticheap INTERFACE)
target_include_directories(elasticheap INTERFACE include)

target_sources(elasticheap INTERFACE
    include/elasticheap/arena_allocator2.h
    README.md
)

if(ELASTICHEAP_ENABLE_BENCHMARK)
    add_executable(elasticheap_benchmark
        benchmark/arena_allocator.cpp
        benchmark/main.cpp
    )

    target_link_libraries(elasticheap_benchmark 
        elasticheap 
        benchmark::benchmark
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(elasticheap_benchmark PRIVATE benchmark)
    if(MSVC)
        target_compile_definitions(elasticheap_benchmark PRIVATE -D_ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()

    target_compile_options(elasticheap_benchmark PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:${CONTAINERS_WARNINGS_GCC}>)
    target_compile_options(elasticheap_benchmark PRIVATE $<$<CXX_COMPILER_ID:MSVC>:${CONTAINERS_WARNINGS_MSVC} /bigobj>)

    target_sanitize(ELASTICHEAP elasticheap_benchmark)
endif()

if(ELASTICHEAP_ENABLE_TESTING)
    add_executable(elasticheap_test
        test/arena_allocator.cpp
    )

    add_test(NAME elasticheap_test COMMAND elasticheap_test)
    target_link_libraries(elasticheap_test 
        elasticheap	
        gtest_main
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(elasticheap_test PRIVATE test)
    if(MSVC)
        target_compile_definitions(elasticheap_test PRIVATE -D_ENABLE_EXTENDED_ALIGNED_STORAGE)
    endif()

    target_compile_options(elasticheap_test PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:${ELASTICHEAP_WARNINGS_GCC}>)
    target_compile_options(elasticheap_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:${ELASTICHEAP_WARNINGS_MSVC} /bigobj>)

    target_sanitize(ELASTICHEAP elasticheap_test)
endif()
