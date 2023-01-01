//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/aligned.h>

#include <atomic>
#include <array>

namespace containers
{
    static_assert(sizeof(aligned< std::atomic< int32_t > >) == 64);
    static_assert(sizeof(aligned< std::atomic< int64_t > >) == 64);

    template< typename T, size_t Alignment = 64 > struct counter
    {
        T operator += (T value)
        {
            return values_[thread::id()].fetch_add(value, std::memory_order_relaxed);
        }

        T get()
        {
            T count = 0;
            for(auto& value: values_)
                count += value.load(std::memory_order_relaxed);
            return count;
        }

    private:
        alignas(Alignment) std::array< aligned< std::atomic< T >, Alignment >, thread::max_threads > values_;
    };

    template< typename T, size_t Frequency = 256, size_t Alignment = 64 > struct frequency_counter
    {
        T operator += (T v)
        {
            auto id = thread::id();
            auto local = counter_[id].local += v;
            if ((counter_[id].n++ & (Frequency - 1)) == 0)
                counter_[id].global.store(local, std::memory_order_relaxed);
            return local;
        }

        T get()
        {
            T value = 0;
            for (auto& counter : counter_)
                value += counter.global.load(std::memory_order_relaxed);
            return value;
        }

    private:
        static_assert((Frequency & (Frequency - 1)) == 0);

        struct counter
        {
            T n = 0;
            T local = 0;
            std::atomic< T > global = 0;
        };

        alignas(Alignment) std::array< aligned< counter, Alignment >, thread::max_threads > counter_;
    };
}
