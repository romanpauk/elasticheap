//
// This file is part of containers project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/bitset.h>
#include <elasticheap/detail/atomic_bitset.h>

#include <benchmark/benchmark.h>

static void bitset(benchmark::State& state) {
    elasticheap::detail::bitset<1<<15> values;

    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j)
            values.set(j);
            
        for(size_t j = 0; j < state.range(); ++j)
            values.clear(j);
    }

    benchmark::DoNotOptimize(values.get(0));
    state.SetItemsProcessed(state.iterations() * state.range());
}

static void atomic_bitset(benchmark::State& state) {
    elasticheap::detail::atomic_bitset<1<<15> values;
    
    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j)
            values.set(j);
            
        for(size_t j = 0; j < state.range(); ++j)
            values.clear(j);
    }

    benchmark::DoNotOptimize(values.get(0));
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK(bitset)->Range(1, 1<<15)->UseRealTime();
BENCHMARK(atomic_bitset)->Range(1, 1<<15)->UseRealTime();


