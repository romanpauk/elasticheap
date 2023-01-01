//
// This file is part of containers project <https://github.com/romanpauk/smart_ptr>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/queue.h>

#include <benchmark/benchmark.h>
#include <thread>
#include <list>
#include <mutex>

static const auto max_threads = containers::thread::max_threads;

// For now, until I have better allocation strategy, use std::list
namespace stl
{
    template< typename T > class queue
    {
    public:
        void push(T value)
        {
            auto guard = std::lock_guard(mutex_);
            queue_.push_back(value);
        }

        bool pop(T& value)
        {
            auto guard = std::lock_guard(mutex_);
            if (!queue_.empty())
            {
                value = std::move(queue_.front());
                queue_.pop_front();
                return true;
            }
            else
            {
                return false;
            }
        }

    private:
        std::mutex mutex_;
        std::list< T > queue_;
    };
}

static void stl_queue(benchmark::State& state)
{
    static stl::queue< int > queue;
    int value;
    for (auto _ : state)
    {
        queue.push(1);
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void stl_queue_pop(benchmark::State& state)
{
    static stl::queue< int > queue;
    int value;
    for (auto _ : state)
    {        
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void hazard_era_queue(benchmark::State& state)
{
    static containers::queue< int > queue;
    int value;
    for (auto _ : state)
    {
        queue.push(1);
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

static void hazard_era_queue_pop(benchmark::State& state)
{
    static containers::queue< int > queue;
    int value;
    for (auto _ : state)
    {        
        queue.pop(value);
    }

    state.SetBytesProcessed(state.iterations());
}

BENCHMARK(stl_queue)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(stl_queue_pop)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_queue)->ThreadRange(1, max_threads)->UseRealTime();
BENCHMARK(hazard_era_queue_pop)->ThreadRange(1, max_threads)->UseRealTime();
