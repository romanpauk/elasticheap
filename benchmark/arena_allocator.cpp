//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/allocators/arena_allocator.h>
#include <containers/allocators/arena_allocator2.h>
#include <containers/allocators/page_allocator.h>

#include <benchmark/benchmark.h>

#include <array>

#define N 24

template< typename Allocator > static void arena_allocator_allocate(benchmark::State& state) {
    struct Class {
        uint8_t data[8];
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
        uint8_t data[8];
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

static void arena_allocator_allocate_uint64_t_arena_only(benchmark::State& state) {
    static uint8_t buffer[1<<20];
    
    auto* arena = reinterpret_cast<containers::arena2< 1<<19, 8, 8 >*>(buffer);
    new (arena) containers::arena2< 1<<19, 8, 8 >();
    std::vector< void* > pointers(state.range());
        
    for (auto _ : state) {
        for(size_t j = 0; j < pointers.size(); ++j)
            pointers[j] = arena->allocate();

        for(size_t j = 0; j < pointers.size(); ++j)
            *(uint64_t*)pointers[j] = j;

        for(size_t j = 0; j < pointers.size(); ++j)
            arena->deallocate(pointers[j]);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

static void arena_allocator_allocate_uint64_t(benchmark::State& state) {
    containers::arena_allocator2< uint64_t > allocator;
    std::vector< uint64_t* > pointers(state.range());
        
    for (auto _ : state) {
        for(size_t j = 0; j < pointers.size(); ++j)
            pointers[j] = allocator.allocate(1);

        for(size_t j = 0; j < pointers.size(); ++j)
            *pointers[j] = j;

        for(size_t j = 0; j < pointers.size(); ++j)
            allocator.deallocate(pointers[j], 1);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}


static void allocator_allocate_uint64_t(benchmark::State& state) {
    std::allocator<uint64_t> allocator;
    std::vector< uint64_t* > pointers(state.range());
    for (auto _ : state) {
        for(size_t j = 0; j < pointers.size(); ++j)
            pointers[j] = allocator.allocate(1);

        for(size_t j = 0; j < pointers.size(); ++j)
            *pointers[j] = j;

        for(size_t j = 0; j < pointers.size(); ++j)
            allocator.deallocate(pointers[j], 1);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK_TEMPLATE(arena_allocator_allocate, std::allocator<char>)->Range(1, 1<<N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, std::allocator<char>)->Range(1, 1<<N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate, containers::page_allocator<char>)->Range(1, 1<<N)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, containers::page_allocator<char>)->Range(1, 1<<N)->UseRealTime();

//BENCHMARK(arena_allocator_allocate_uint64_t_arena_only)->Range(1, 1<<15)->UseRealTime();
BENCHMARK(arena_allocator_allocate_uint64_t)->Range(1, 1<<N)->UseRealTime();
BENCHMARK(allocator_allocate_uint64_t)->Range(1, 1<<N)->UseRealTime();