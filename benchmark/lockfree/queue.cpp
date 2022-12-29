//
// This file is part of containers project <https://github.com/romanpauk/smart_ptr>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/queue.h>

#include <benchmark/benchmark.h>
#include <thread>

static const auto max_threads = std::thread::hardware_concurrency();

static void hazard_era_queue(benchmark::State& state)
{
    static containers::queue< int > queue;
    int value;
    for (auto _ : state)
    {
        queue.push(1);
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void hazard_era_queue_pop(benchmark::State& state)
{
    static containers::queue< int > queue;
    int value;
    for (auto _ : state)
    {        
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

BENCHMARK(hazard_era_queue)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_queue_pop)->ThreadRange(1, max_threads)->UseRealTime();