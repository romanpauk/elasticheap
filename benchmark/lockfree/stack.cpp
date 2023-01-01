//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/stack.h>
#include <containers/lockfree/counter.h>

#include <benchmark/benchmark.h>
#include <thread>
#include <mutex>
#include <stack>
#include <list>

static const auto max_threads = containers::thread::max_threads;

// For now, until I have better allocation strategy, use std::list
namespace stl
{
    template< typename T > class stack
    {
    public:
        void push(T value)
        {
            auto guard = std::lock_guard(mutex_);
            stack_.push_back(value);
        }

        bool pop(T& value)
        {
            auto guard = std::lock_guard(mutex_);
            if (!stack_.empty())
            {
                value = std::move(stack_.back());
                stack_.pop_back();
                return true;
            }
            else
            {
                return false;
            }
        }

    private:
        std::mutex mutex_;
        std::list< T > stack_;
    };
}

static void stl_stack(benchmark::State& state)
{
    static stl::stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void stl_stack_pop(benchmark::State& state)
{
    static stl::stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void hazard_era_stack(benchmark::State& state)
{
    static containers::stack< int > stack;

    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void hazard_era_stack_pop(benchmark::State& state)
{
    static containers::stack< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

/*
// TODO: stack_eb uses two different allocators, so the trick with reinterpret_cast
// will not work. One option is to have single allocator for all pointers and distinguish
// them later. That means eash hazard_buffer would need to take type tag.

static void hazard_era_stack_eb(benchmark::State& state)
{
    static containers::stack_eb< int > stack;
    int value;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}
*/

BENCHMARK(stl_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(stl_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK(hazard_era_stack_eb)->ThreadRange(1, max_threads)->UseRealTime();

template < typename T > struct function_thread_local
{
    static T& instance() { static thread_local T value; return value; }
};

template < typename T > struct class_thread_local
{
    static T& instance() { return value; }
    static thread_local T value;
};

template< typename T > thread_local T class_thread_local< T >::value;

template < typename Class > static void thread_local_benchmark(benchmark::State& state)
{
    Class::instance() = 1;
    volatile int value = 0;
    for (auto _ : state)
    {
        value += Class::instance();
    }

    benchmark::DoNotOptimize(value);
    state.SetItemsProcessed(state.iterations());

}

//BENCHMARK_TEMPLATE(thread_local_benchmark, function_thread_local< int >)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK_TEMPLATE(thread_local_benchmark, class_thread_local< int >)->ThreadRange(1, max_threads)->UseRealTime();
