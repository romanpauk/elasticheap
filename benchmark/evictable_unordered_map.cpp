//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/evictable_unordered_map.h>

#include <benchmark/benchmark.h>

#include <unordered_map>

#define N 1ull << 20

template< typename Container > static void container_emplace(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            container.emplace(i, i);
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_find(benchmark::State& state) {
    Container container;
    for (size_t i = 0; i < (size_t)state.range(); ++i) {
        container.emplace(i, i);
    }

    bool result = true;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            result &= container.find(i) != container.end();
        }
    }
    
    state.SetItemsProcessed(result ? state.iterations() * state.range() : 0);
}

BENCHMARK_TEMPLATE(container_emplace, std::unordered_map< size_t, size_t >)->Range(1, N);
BENCHMARK_TEMPLATE(container_emplace, containers::evictable_unordered_map< size_t, size_t >)->Range(1, N);

BENCHMARK_TEMPLATE(container_find, std::unordered_map< size_t, size_t >)->Range(1, N);
BENCHMARK_TEMPLATE(container_find, containers::evictable_unordered_map< size_t, size_t >)->Range(1, N);
