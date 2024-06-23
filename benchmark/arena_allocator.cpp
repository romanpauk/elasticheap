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

static void arena_allocator_allocate_uint64_t(benchmark::State& state) {
    for (auto _ : state) {
        containers::arena_allocator2< uint64_t > allocator;
    
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            uint64_t* p1 = allocator.allocate(1);
            uint64_t* p2 = allocator.allocate(1);
            uint64_t* p3 = allocator.allocate(1);
            uint64_t* p4 = allocator.allocate(1);
            uint64_t* p5 = allocator.allocate(1);
            uint64_t* p6 = allocator.allocate(1);
        #if 1        
            allocator.deallocate(p1, 1);
            allocator.deallocate(p2, 1);
            allocator.deallocate(p3, 1);
            allocator.deallocate(p4, 1);
            allocator.deallocate(p5, 1);
            allocator.deallocate(p6, 1); 
        #else
            allocator.deallocate(p6, 1);
            allocator.deallocate(p5, 1);
            allocator.deallocate(p4, 1);
            allocator.deallocate(p3, 1);
            allocator.deallocate(p2, 1);
            allocator.deallocate(p1, 1);
        #endif
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range() * 6);
}


static void allocator_allocate_uint64_t(benchmark::State& state) {
    std::allocator<uint64_t> allocator;
    uintptr_t ptr = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            uint64_t* p1 = allocator.allocate(1);
            uint64_t* p2 = allocator.allocate(1);
            uint64_t* p3 = allocator.allocate(1);
            uint64_t* p4 = allocator.allocate(1);
            uint64_t* p5 = allocator.allocate(1);
            uint64_t* p6 = allocator.allocate(1);
            
        #if 1
            allocator.deallocate(p1, 1);
            allocator.deallocate(p2, 1);
            allocator.deallocate(p3, 1);
            allocator.deallocate(p4, 1);
            allocator.deallocate(p5, 1);
            allocator.deallocate(p6, 1);
        #else
            allocator.deallocate(p6, 1);
            allocator.deallocate(p5, 1);
            allocator.deallocate(p4, 1);
            allocator.deallocate(p3, 1);
            allocator.deallocate(p2, 1);
            allocator.deallocate(p1, 1); 
        #endif
        }
    }

    benchmark::DoNotOptimize(ptr);
    state.SetItemsProcessed(state.iterations() * state.range() * 6);
}

BENCHMARK_TEMPLATE(arena_allocator_allocate, std::allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, std::allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate, containers::page_allocator<char>)->Range(1, 1<<24)->UseRealTime();
BENCHMARK_TEMPLATE(arena_allocator_allocate_nobuffer, containers::page_allocator<char>)->Range(1, 1<<24)->UseRealTime();

BENCHMARK(arena_allocator_allocate_uint64_t)->Range(1, 1<<24)->UseRealTime();
BENCHMARK(allocator_allocate_uint64_t)->Range(1, 1<<24)->UseRealTime();