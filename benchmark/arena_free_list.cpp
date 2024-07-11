//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/allocator.h>

#include <benchmark/benchmark.h>

static void arena_free_list_uint16_t(benchmark::State& state) {
    uint32_t size = 0;
    elasticheap::arena_free_list< uint16_t, 1<<15 > values;

    uint16_t tmp = 0;
    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j)
            values.push(j, size);
            
        for(size_t j = 0; j < state.range(); ++j)
            tmp = values.pop(size);
    }

    benchmark::DoNotOptimize(tmp);
    state.SetItemsProcessed(state.iterations() * state.range());
}

static void arena_free_list_uint32_t(benchmark::State& state) {
    uint32_t size = 0;
    static elasticheap::arena_free_list< uint32_t, 1ull<<31 > values;

    uint32_t tmp = 0;
    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j)
            values.push(j, size);
            
        for(size_t j = 0; j < state.range(); ++j)
            tmp = values.pop(size);
    }

    benchmark::DoNotOptimize(tmp);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK(arena_free_list_uint16_t)->Range(1, 1<<15)->UseRealTime();
BENCHMARK(arena_free_list_uint32_t)->Range(1, 1ull<<31)->UseRealTime();
