//
// This file is part of containers project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/allocator.h>

#include <benchmark/benchmark.h>

#define N 26

static void page_manager_allocate_deallocate(benchmark::State& state) {
    static elasticheap::page_manager<1<<21,1<<18,1ull<<40> page_manager;
    std::vector< void* > pages(state.range());

    for (auto _ : state) {
        for(size_t j = 0; j < pages.size(); ++j)
            pages[j] = page_manager.allocate_page();

        for(size_t j = 0; j < pages.size(); ++j)
            page_manager.deallocate_page(pages[j]);
    }

    state.SetItemsProcessed(state.iterations() * state.range());
}

BENCHMARK(page_manager_allocate_deallocate)->Range(1, 1<<N)->UseRealTime();
