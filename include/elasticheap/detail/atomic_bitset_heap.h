//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/atomic_bitset.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <tuple>

namespace elasticheap::detail {

template< typename T, std::size_t Capacity > struct atomic_bitset_heap {
    static_assert(Capacity <= std::numeric_limits<uint32_t>::max());

    static constexpr std::size_t capacity() { return Capacity; }

    atomic_bitset_heap() {
        bitmap_.clear();
        range_.store(Capacity, std::memory_order_relaxed);
    }

    void push(T value) {
        auto range = range_.load(std::memory_order_acquire);

        assert(value < Capacity);
        assert(!bitmap_.get(value));
        bitmap_.set(value);
        
        while(true) {
            auto [max, min] = unpack(range);
            if (max >= value && min <= value) {
                std::atomic_thread_fence(std::memory_order_release);
                return;
            }
            
            if(range_.compare_exchange_strong(range, pack(std::max<T>(max, value), std::min<T>(min, value)), std::memory_order_release)) {
                return;
            }
        }
    }

    bool empty() const {
        return (uint32_t)range_.load(std::memory_order_relaxed) == Capacity;
    }

    bool pop(T& value) {
        auto range = range_.load(std::memory_order_acquire);
    again:
        auto [max, min] = unpack(range);
        if (min < Capacity) {
            for(std::size_t i = min + 1; i <= max; ++i) {
                if (bitmap_.get(i)) {
                    if (range_.compare_exchange_strong(range, pack(max, i), std::memory_order_relaxed)) {
                        bitmap_.clear(min);
                        value = min;
                        return true;
                    }
                    
                    goto again;
                }
            }

            if (range_.compare_exchange_strong(range, Capacity, std::memory_order_relaxed)) {
                value = min;
                return true;
            } else {
                goto again;
            }
        }

        return false;
    }

private:
    std::tuple< uint32_t, uint32_t > unpack(uint64_t range) {
        return { range >> 32, range };
    }

    uint64_t pack(uint32_t max, uint32_t min) {
        return ((uint64_t)max << 32) | min;
    }

    std::atomic<uint64_t> range_;
    alignas(64) detail::atomic_bitset<Capacity> bitmap_;
};

}