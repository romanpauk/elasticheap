//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/allocators/arena_allocator.h>
#include <containers/allocators/page_allocator.h>

#include <benchmark/benchmark.h>

template< typename Allocator > static void arena_allocator_allocate(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };
      
    static uint8_t buffer[1<<17];
    uintptr_t ptr = 0;
    for (auto _ : state) {
        containers::arena< Allocator > arena(buffer);
        containers::arena_allocator< Class, decltype(arena) > allocator(arena);

        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Allocator > static void arena_allocator_allocate_nobuffer(benchmark::State& state) {
    struct Class {
        uint8_t data[64];
    };
        
    uintptr_t ptr = 0;
    for (auto _ : state) {
        containers::arena< Allocator > arena;
        containers::arena_allocator< Class, decltype(arena) > allocator(arena);
    
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            ptr += (uintptr_t)allocator.allocate(1);
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(arena_allocator_allocate, std::allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, std::allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate, containers::page_allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, containers::page_allocator<char>)->Range(1, 1<<24)->UseRealTime();
