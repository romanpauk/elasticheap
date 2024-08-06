//
// This file is part of containers project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/bitset_heap.h>
#include <elasticheap/detail/atomic_bitset_heap.h>

#include <benchmark/benchmark.h>

static void bitset_heap(benchmark::State& state) {
    elasticheap::detail::bitset_heap<uint16_t, 1<<15> values;

    uint16_t tmp = 0;
    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j) {
            values.push(j);
        }

        for(size_t j = 0; j < state.range(); ++j) {
            tmp = values.pop();
        }
    }

    benchmark::DoNotOptimize(tmp);
    state.SetItemsProcessed(state.iterations() * state.range());
}

static void atomic_bitset_heap(benchmark::State& state) {
    std::atomic<uint64_t> range;
    elasticheap::detail::atomic_bitset_heap<uint16_t, 1<<15> values(range);
    uint16_t tmp = 0;
    for (auto _ : state) {
        for(size_t j = 0; j < state.range(); ++j) {
            values.push(range, j);
        }

        for(size_t j = 0; j < state.range(); ++j) {
            values.pop(range, tmp);
        }
    }

    benchmark::DoNotOptimize(tmp);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK(bitset_heap)->Range(1, 1<<15)->UseRealTime();
BENCHMARK(atomic_bitset_heap)->Range(1, 1<<15)->UseRealTime();
