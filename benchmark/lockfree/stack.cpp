//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/bounded_stack.h>
#include <containers/lockfree/unbounded_stack.h>

#include <benchmark/benchmark.h>
#include <thread>
#include <mutex>
#include <stack>
#include <list>
#include <random>

static const auto max_threads = std::thread::hardware_concurrency();
static const auto iterations = 1024;

template< typename T > class stl_stack
{
public:
    using value_type = T;

    void push(T value)
    {
        auto guard = std::lock_guard(mutex_);
        stack_.push(value);
    }

    bool pop(T& value)
    {
        auto guard = std::lock_guard(mutex_);
        if (!stack_.empty())
        {
            value = std::move(stack_.top());
            stack_.pop();
            return true;
        }
        else
        {
            return false;
        }
    }

    bool empty() const
    {
        auto guard = std::lock_guard(mutex_);
        return stack_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::stack< T > stack_;
};

template< typename Stack > static void stack_push_pop(benchmark::State& state)
{
    static Stack stack;
    int value = 0;
    for (auto _ : state)
    {
        stack.push(1);
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

template< typename Stack > static void stack_push_pop_rand(benchmark::State& state)
{
    std::minstd_rand r;

    static Stack stack;
    typename Stack::value_type value{}, result{};
    for (auto _ : state)
    {
        if (r() & 1)
            stack.push(value++);
        else
            stack.pop(result);
    }

    state.SetBytesProcessed(state.iterations() * 2);
}

template< typename Stack > static void stack_push(benchmark::State& state)
{
    for (auto _ : state)
    {
        Stack stack;
        for (size_t i = 0; i < stack.capacity(); ++i) {
            stack.push(1);
        }
    }
    state.SetBytesProcessed(state.iterations() * Stack::capacity());
}

template< typename Stack > static void stack_pop(benchmark::State& state)
{
    static Stack stack;
    typename Stack::value_type value;
    for (auto _ : state)
    {
        stack.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

template< typename Stack > static void stack_empty(benchmark::State& state)
{
    static Stack stack;
    typename Stack::value_type value;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(stack.empty());
    }

    state.SetBytesProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(stack_push_pop, stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push_pop_rand, stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop, stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_empty, stl_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop,containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push_pop_rand, containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop, containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_empty, containers::unbounded_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop, containers::bounded_stack<int, 1024>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push_pop_rand, containers::bounded_stack<int, 1024>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push, containers::bounded_stack<int, 1024>)->Iterations(iterations)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop, containers::bounded_stack<int, 1024>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_empty, containers::bounded_stack<int, 1024>)->ThreadRange(1, max_threads)->UseRealTime();

BENCHMARK_TEMPLATE(stack_push_pop, containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_push_pop_rand, containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_pop, containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(stack_empty, containers::unbounded_blocked_stack<int>)->ThreadRange(1, max_threads)->UseRealTime();
