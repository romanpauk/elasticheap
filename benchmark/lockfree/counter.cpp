//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/stack.h>
#include <containers/lockfree/counter.h>

#include <benchmark/benchmark.h>

static const auto max_threads = containers::thread::max_threads;

static void counter64(benchmark::State& state)
{
    static containers::counter< uint64_t, 64 > counter;
    int value;
    for (auto _ : state)
    {
        counter += 1;
    }

    state.SetBytesProcessed(state.iterations());
}

static void counter128(benchmark::State& state)
{
    static containers::counter< uint64_t, 128 > counter;
    int value;
    for (auto _ : state)
    {
        counter += 1;
    }

    state.SetBytesProcessed(state.iterations());
}

static void frequency_counter_64(benchmark::State& state)
{
    static containers::frequency_counter< uint64_t > counter;
    int value;
    for (auto _ : state)
    {
        counter += 1;
    }

    state.SetBytesProcessed(state.iterations());
}

//BENCHMARK(counter64)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK(counter128)->ThreadRange(1, max_threads)->UseRealTime();
//BENCHMARK(frequency_counter_64)->ThreadRange(1, max_threads)->UseRealTime();
