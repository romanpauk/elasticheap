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

    atomic_bitset_heap(std::atomic<uint64_t>& range) {
        bitmap_.clear();
        range.store(Capacity, std::memory_order_relaxed);
    }

    void push(std::atomic<uint64_t>& range, T value) {
        auto r = range.load(std::memory_order_acquire);

        assert(value < Capacity);
        assert(!bitmap_.get(value));
        bitmap_.set(value);

        while(true) {
            auto [max, min] = unpack(r);
            if (max >= value && min <= value) {
                std::atomic_thread_fence(std::memory_order_release);
                return;
            }

            if(range.compare_exchange_strong(r, pack(std::max<T>(max, value), std::min<T>(min, value)), std::memory_order_release)) {
                return;
            }
        }
    }

    bool empty(std::atomic<uint64_t>& range) const {
        return (uint32_t)range.load(std::memory_order_relaxed) == Capacity;
    }

    bool pop(std::atomic<uint64_t>& range, T& value) {
        auto r = range.load(std::memory_order_acquire);
    again:
        auto [max, min] = unpack(range);
        if (min < Capacity) {
            for(std::size_t i = min; i < max; ++i) {
                if (bitmap_.get(i)) {
                    if (range.compare_exchange_strong(r, pack(max, i + 1), std::memory_order_relaxed)) {
                        bitmap_.clear(i);
                        value = i;
                        return true;
                    }

                    goto again;
                }
            }

            if (range.compare_exchange_strong(r, Capacity, std::memory_order_relaxed)) {
                bitmap_.clear(min);
                value = min;
                return true;
            } else {
                goto again;
            }
        }

        return false;
    }

    bool get(T value) const {
        return bitmap_.get(value);
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
