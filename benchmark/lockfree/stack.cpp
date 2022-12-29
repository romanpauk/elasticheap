//
// This file is part of containers project <https://github.com/romanpauk/smart_ptr>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/stack.h>

#include <benchmark/benchmark.h>
#include <thread>

static const auto max_threads = std::thread::hardware_concurrency();

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

BENCHMARK(hazard_era_stack)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_stack_pop)->ThreadRange(1, max_threads)->UseRealTime();
