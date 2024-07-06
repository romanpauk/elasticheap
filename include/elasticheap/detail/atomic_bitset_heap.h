//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/atomic_bitset.h>

#include <algorithm>
#include <cassert>
#include <tuple>

namespace elasticheap::detail {

template< typename T, std::size_t Capacity > struct atomic_bitset_heap {
    static_assert(Capacity <= std::numeric_limits<uint32_t>::max());

    static constexpr std::size_t capacity() { return Capacity; }

    atomic_bitset_heap() {
        range_.store(Capacity, std::memory_order_relaxed);
        bitmap_.clear();
    }

    void push(T value) {
        assert(value < Capacity);
        assert(!bitmap_.get(value));
        bitmap_.set(value);
        
        auto range = range_.load(std::memory_order_acquire);
        auto [max, min] = extract(range);
        range_.compare_exchange_strong(range,
            ((uint64_t)std::max(max, value) << 32) | std::min(min, value));
    }

    bool empty() const {
        return (uint32_t)range_.load(std::memory_order_relaxed) == Capacity;
    }

    bool pop(T& value) {
        auto range = range_.load(std::memory_order_acquire);
    again:
        auto [max, min] = extract(range);
        if (min < Capacity) {
            for(std::size_t i = min + 1; i <= max; ++i) {
                if (bitmap_.get(i)) {
                    if (range_.compare_exchange_strong(range, ((uint64_t)max << 32 | i))) {
                        bitmap_.clear(min);
                        value = min;
                        return true;
                    }
                    
                    goto again;
                }
            }

            if (range_.compare_exchange_strong(range, Capacity)) {
                value = min;
                return true;
            } else {
                goto again;
            }
        }

        return false;
    }

private:
    std::tuple< uint32_t, uint32_t > extract(uint64_t range) {
        return { range >> 32, range };
    }

    std::atomic<uint64_t> range_;
    alignas(64) detail::atomic_bitset<Capacity> bitmap_;
};

}