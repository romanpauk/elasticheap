//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/counter.h>
#include <containers/lockfree/detail/thread_manager.h>

#include <thread>

#include <benchmark/benchmark.h>

#include "factory.h"

static const auto max_threads = std::thread::hardware_concurrency();

template< typename T > static void counter_increment(benchmark::State& state) {
    auto& counter = factory< T >::get();
    auto thread_id = containers::detail::thread::id();
    for (auto _ : state) {
        counter.inc(1, thread_id);
    }

    volatile auto result = counter.get();
    if (state.thread_index() == 0) {
        state.SetBytesProcessed(result);
        factory< T >::reset();
    }
}

BENCHMARK_TEMPLATE(counter_increment, containers::counter<std::atomic<uint64_t>, containers::detail::thread::max_threads>)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK_TEMPLATE(counter_increment, containers::counter<uint64_t, containers::detail::thread::max_threads>)->ThreadRange(1, max_threads)->UseRealTime();