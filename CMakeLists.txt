cmake_minimum_required(VERSION 3.22)
project(containers VERSION 0.1)
include(FetchContent)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS NO)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
enable_testing()
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
    include/containers/lockfree/atomic16.h
    include/containers/lockfree/bounded_queue.h
    include/containers/lockfree/bounded_queue_bbq.h
    include/containers/lockfree/bounded_stack.h
    include/containers/lockfree/unbounded_queue.h
    include/containers/lockfree/unbounded_stack.h
    include/containers/lockfree/detail/aligned.h
    include/containers/lockfree/detail/atomic16_win32.h
    include/containers/lockfree/detail/exponential_backoff.h
    include/containers/lockfree/detail/hazard_era_allocator.h
    include/containers/lockfree/detail/optional.h
    README.md
)

if(CONTAINERS_ENABLE_BENCHMARK)
    add_executable(containers_benchmark
        benchmark/main.cpp
        benchmark/lockfree/atomic.cpp
        benchmark/lockfree/stack.cpp
        benchmark/lockfree/queue.cpp
    )

    target_link_libraries(containers_benchmark 
        containers 
        benchmark::benchmark
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(containers_benchmark PRIVATE benchmark)
endif()

if(CONTAINERS_ENABLE_TESTING)
    add_executable(containers_test
        test/atomic.cpp
        test/queue.cpp
        test/stack.cpp
    )

    add_test(NAME containers_test COMMAND containers_test)
    target_link_libraries(containers_test 
        containers	
        gtest_main
        $<$<PLATFORM_ID:Linux>:atomic>
    )
    target_include_directories(containers_test PRIVATE test)
endif()
