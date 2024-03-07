//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/growable_array.h>

#if !defined(_WIN32)
#include <containers/mmapped_array.h>
#endif

#include <benchmark/benchmark.h>
#include <deque>
#include <vector>
#include <thread>

#define N 1ull << 20

std::mutex mutex;

size_t consume(size_t value) { return value; }
size_t consume(const std::string& value) { return value.empty(); }

template< typename Container > static void container_push_back_locked(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            std::lock_guard lock(mutex);
            container.emplace_back(typename Container::value_type{});
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_push_back(benchmark::State& state) {
    for (auto _ : state) {
        Container container;
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            container.emplace_back(typename Container::value_type{});
    }
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access(benchmark::State& state) {
    Container container;
    container.push_back(typename Container::value_type{});
    size_t result = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i)
            result += consume(container[0]);
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access_local(benchmark::State& state) {
    Container container;
    container.push_back(typename Container::value_type{});
    size_t result = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            static thread_local typename Container::reader_state reader;
            result += consume(container.read(reader, 0));
        }
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}

template< typename Container > static void container_indexed_access_locked(benchmark::State& state) {
    Container container;
    container.push_back(typename Container::value_type{});
    size_t result = 0;
    for (auto _ : state) {
        for (size_t i = 0; i < (size_t)state.range(); ++i) {
            std::lock_guard lock(mutex);
            result += consume(container[0]);
        }
    }
    benchmark::DoNotOptimize(result);
    state.SetItemsProcessed(state.iterations() * state.range());
}


BENCHMARK_TEMPLATE(container_push_back_locked, std::vector<size_t>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back_locked, std::vector<std::string>)->Range(1, N);

BENCHMARK_TEMPLATE(container_push_back_locked, std::deque<size_t>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back_locked, std::deque<std::string>)->Range(1, N);

BENCHMARK_TEMPLATE(container_push_back, containers::growable_array<size_t>)->Range(1, N);
//BENCHMARK_TEMPLATE(container_push_back, containers::growable_array<std::string>)->Range(1, N);

#if !defined(_WIN32)
BENCHMARK_TEMPLATE(container_push_back, containers::mmapped_array<size_t>)->Range(1, N);
#endif
//BENCHMARK_TEMPLATE(container_push_back, containers::mmapped_array<std::string>)->Range(1, N);

BENCHMARK_TEMPLATE(container_indexed_access, containers::growable_array<size_t>)->Range(1, N);

#if !defined(_WIN32)
BENCHMARK_TEMPLATE(container_indexed_access, containers::mmapped_array<size_t>)->Range(1, N);
#endif

BENCHMARK_TEMPLATE(container_indexed_access_local, containers::growable_array<size_t>)->Range(1, N);

#if !defined(_WIN32)
BENCHMARK_TEMPLATE(container_indexed_access_local, containers::mmapped_array<size_t>)->Range(1, N);
#endif

BENCHMARK_TEMPLATE(container_indexed_access_locked, std::deque<size_t>)->Range(1, N);
