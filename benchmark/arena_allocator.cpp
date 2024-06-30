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

#define N 26

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

#if defined(STATS)
    containers::print_stats();
#endif
}

static void arena_allocator_allocate_sizes(benchmark::State& state) {
    containers::arena_allocator2< std::array< uint64_t, 1> > allocator1;
    std::vector< std::array< uint64_t, 1>* > pointers1(state.range());
    
    containers::arena_allocator2< std::array< uint64_t, 2> > allocator2;
    std::vector< std::array< uint64_t, 2>* > pointers2(state.range());
    
    containers::arena_allocator2< std::array< uint64_t, 3> > allocator3;
    std::vector< std::array< uint64_t, 3>* > pointers3(state.range());
    
    containers::arena_allocator2< std::array< uint64_t, 4> > allocator4;
    std::vector< std::array< uint64_t, 4>* > pointers4(state.range());    
    
    containers::arena_allocator2< std::array< uint64_t, 5> > allocator5;
    std::vector< std::array< uint64_t, 5>* > pointers5(state.range());    
    
    containers::arena_allocator2< std::array< uint64_t, 6> > allocator6;
    std::vector< std::array< uint64_t, 6>* > pointers6(state.range());    
    
    for (auto _ : state) {
        for(int j = 0; j < state.range(); ++j) {
            pointers1[j] = allocator1.allocate(1);
            pointers2[j] = allocator2.allocate(1);
            pointers3[j] = allocator3.allocate(1);
            pointers4[j] = allocator4.allocate(1);
            //pointers5[j] = allocator5.allocate(1);
            //pointers6[j] = allocator6.allocate(1);
        }

        for(int j = 0; j < state.range(); ++j) {
            (*pointers1[j])[0] = j;
            (*pointers2[j])[0] = j;
            (*pointers3[j])[0] = j;
            (*pointers4[j])[0] = j;
            //(*pointers5[j])[0] = j;
            //(*pointers6[j])[0] = j;
        }

        for(int j = 0; j < state.range(); ++j) {
            allocator1.deallocate(pointers1[j], 1);
            allocator2.deallocate(pointers2[j], 1);
            allocator3.deallocate(pointers3[j], 1);
            allocator4.deallocate(pointers4[j], 1);
            //allocator5.deallocate(pointers5[j], 1);
            //allocator6.deallocate(pointers6[j], 1);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range() * 4);

#if defined(STATS)
    containers::print_stats();
#endif
}

void allocator_allocate_sizes(benchmark::State& state) {
    std::allocator< std::array< uint64_t, 1> > allocator1;
    std::vector< std::array< uint64_t, 1>* > pointers1(state.range());
    
    std::allocator< std::array< uint64_t, 2> > allocator2;
    std::vector< std::array< uint64_t, 2>* > pointers2(state.range());
    
    std::allocator< std::array< uint64_t, 3> > allocator3;
    std::vector< std::array< uint64_t, 3>* > pointers3(state.range());
    
    std::allocator< std::array< uint64_t, 4> > allocator4;
    std::vector< std::array< uint64_t, 4>* > pointers4(state.range());    
    
    std::allocator< std::array< uint64_t, 5> > allocator5;
    std::vector< std::array< uint64_t, 5>* > pointers5(state.range());    
    
    std::allocator< std::array< uint64_t, 6> > allocator6;
    std::vector< std::array< uint64_t, 6>* > pointers6(state.range());    
    
    for (auto _ : state) {
        for(int j = 0; j < state.range(); ++j) {
            pointers1[j] = allocator1.allocate(1);
            pointers2[j] = allocator2.allocate(1);
            pointers3[j] = allocator3.allocate(1);
            pointers4[j] = allocator4.allocate(1);
            //pointers5[j] = allocator5.allocate(1);
            //pointers6[j] = allocator6.allocate(1);
        }

        for(int j = 0; j < state.range(); ++j) {
            (*pointers1[j])[0] = j;
            (*pointers2[j])[0] = j;
            (*pointers3[j])[0] = j;
            (*pointers4[j])[0] = j;
            //(*pointers5[j])[0] = j;
            //(*pointers6[j])[0] = j;
        }

        for(int j = 0; j < state.range(); ++j) {
            allocator1.deallocate(pointers1[j], 1);
            allocator2.deallocate(pointers2[j], 1);
            allocator3.deallocate(pointers3[j], 1);
            allocator4.deallocate(pointers4[j], 1);
            //allocator5.deallocate(pointers5[j], 1);
            //allocator6.deallocate(pointers6[j], 1);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range() * 4);
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
BENCHMARK(arena_allocator_allocate_sizes)->Range(1, 1<<N)->UseRealTime();
BENCHMARK(allocator_allocate_sizes)->Range(1, 1<<N)->UseRealTime();

BENCHMARK(allocator_allocate_uint64_t)->Range(1, 1<<N)->UseRealTime();