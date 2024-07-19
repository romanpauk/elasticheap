//
// This file is part of containers project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/allocator.h>

#include <benchmark/benchmark.h>

#include <array>

#define N 26

static void arena_allocator_allocate_uint64_t_arena_only(benchmark::State& state) {
    static uint8_t buffer[1<<20];
#if 0
    elasticheap::arena_descriptor desc;
    auto* arena = reinterpret_cast<elasticheap::arena< 1<<17, 8, 8 >*>(buffer);
    new (arena) elasticheap::arena< 1<<17, 8, 8 >(desc);
    std::vector< void* > pointers(state.range());

    for (auto _ : state) {
        for(size_t j = 0; j < pointers.size(); ++j)
            pointers[j] = arena->allocate(desc);

        for(size_t j = 0; j < pointers.size(); ++j)
            *(uint64_t*)pointers[j] = j;

        for(size_t j = 0; j < pointers.size(); ++j)
            arena->deallocate(desc, pointers[j]);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
#endif
}

static void arena_allocator_allocate_uint64_t(benchmark::State& state) {
    elasticheap::allocator< uint64_t > allocator;
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
    elasticheap::print_stats();
#endif
}

static void arena_allocator_allocate_sizes(benchmark::State& state) {
    elasticheap::allocator< std::array< uint64_t, 1> > allocator1;
    std::vector< std::array< uint64_t, 1>* > pointers1(state.range());

    elasticheap::allocator< std::array< uint64_t, 2> > allocator2;
    std::vector< std::array< uint64_t, 2>* > pointers2(state.range());

    elasticheap::allocator< std::array< uint64_t, 3> > allocator3;
    std::vector< std::array< uint64_t, 3>* > pointers3(state.range());

    elasticheap::allocator< std::array< uint64_t, 4> > allocator4;
    std::vector< std::array< uint64_t, 4>* > pointers4(state.range());

    elasticheap::allocator< std::array< uint64_t, 5> > allocator5;
    std::vector< std::array< uint64_t, 5>* > pointers5(state.range());

    elasticheap::allocator< std::array< uint64_t, 6> > allocator6;
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
    elasticheap::print_stats();
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

//BENCHMARK(arena_allocator_allocate_uint64_t_arena_only)->Range(1, 1<<15)->UseRealTime();
BENCHMARK(arena_allocator_allocate_uint64_t)->Range(1, 1<<N)->UseRealTime();
BENCHMARK(arena_allocator_allocate_sizes)->Range(1, 1<<N)->UseRealTime();
BENCHMARK(allocator_allocate_sizes)->Range(1, 1<<N)->UseRealTime();
BENCHMARK(allocator_allocate_uint64_t)->Range(1, 1<<N)->UseRealTime();
