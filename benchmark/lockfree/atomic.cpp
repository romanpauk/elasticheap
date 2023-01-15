//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/atomic16.h>

#include <benchmark/benchmark.h>
#include <thread>

static const auto max_threads = std::thread::hardware_concurrency();

template< typename AtomicType > static void atomic_load(benchmark::State& state)
{
    static AtomicType value;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(value.load());
    }
    state.SetItemsProcessed(state.iterations());
}

struct pointer
{
    uintptr_t ptr;
    uintptr_t counter;
};

BENCHMARK_TEMPLATE(atomic_load, std::atomic<uint64_t>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(atomic_load, containers::atomic16<pointer>)->ThreadRange(1, max_threads)->UseRealTime();

template< typename AtomicType > void atomic_store(benchmark::State& state)
{
    static AtomicType value;
    for (auto _ : state)
    {
        value.store({});
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(atomic_store, std::atomic<uint64_t>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(atomic_store, containers::atomic16<pointer>)->ThreadRange(1, max_threads)->UseRealTime();

template< typename AtomicType > static void atomic_compare_exchange_strong(benchmark::State& state)
{
    static AtomicType value;
    static typename AtomicType::value_type expected;
    static thread_local typename AtomicType::value_type desired;

    for (auto _ : state)
    {
        value.compare_exchange_strong(expected, desired);
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(atomic_compare_exchange_strong, std::atomic<uint64_t>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(atomic_compare_exchange_strong, containers::atomic16<pointer>)->ThreadRange(1, max_threads)->UseRealTime();
