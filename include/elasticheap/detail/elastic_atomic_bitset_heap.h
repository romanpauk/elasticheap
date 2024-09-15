//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/atomic_bitset.h>
#include <elasticheap/detail/elastic_atomic_array.h>
#include <elasticheap/detail/utils.h>

#include <atomic>
#include <cstdint>
#include <tuple>

#include <sys/mman.h>

namespace elasticheap::detail {

template< typename T, std::size_t Capacity, std::size_t PageSize > struct elastic_atomic_bitset_heap {
    static constexpr std::size_t MmapSize = sizeof(detail::atomic_bitset<Capacity>) + PageSize - 1;
    static constexpr std::size_t PageCount = (Capacity + PageSize * 8 - 1) / (PageSize * 8);

    static_assert(Capacity <= std::numeric_limits<uint32_t>::max());

    static constexpr std::size_t capacity() { return Capacity; }

    elastic_atomic_bitset_heap()
        : mmap_((uint8_t*)mmap(0, MmapSize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
    {
        if (mmap_ == MAP_FAILED)
            __failure("mmap");
        bitmap_ = (detail::atomic_bitset<Capacity>*)align<PageSize>(mmap_);
        range_.store(Capacity, std::memory_order_relaxed);
    }

    void push(T value) {
        assert(value < Capacity);

        storage_.acquire(page(value), (uint8_t*)bitmap_ + page(value) * PageSize);

        // TODO: thread safety, check that set succeeded and if not, release
        assert(!bitmap_->get(value));
        bitmap_->set(value);

        auto r = range_.load(std::memory_order_acquire);
        while(true) {
            auto [max, min] = unpack(r);
            if (max >= value && min <= value) {
                std::atomic_thread_fence(std::memory_order_release);
                return;
            }

            if(range_.compare_exchange_strong(r, pack(std::max<T>(max, value), std::min<T>(min, value)), std::memory_order_release)) {
                return;
            }
        }
    }

    bool empty() const {
        return (uint32_t)range_.load(std::memory_order_relaxed) == Capacity;
    }

    bool erase(T value) {
        if (!get(value))
            return false;

        bool cleared = bitmap_->clear(value);
        if (cleared) {
            storage_.release(page(value), (uint8_t*)bitmap_ + page(value) * PageSize);

            // TODO: recalculate the range
            // Note that there is an use-case of erase/push that does not modify the range
            std::atomic_thread_fence(std::memory_order_release);
        }
        return cleared;

        auto r = range_.load(std::memory_order_acquire);
        auto [max, min] = unpack(r);
        if (min == value) {
            for (std::size_t i = min + 1; i < max; ++i) {
                if (get(i)) {
                    if (range_.compare_exchange_strong(r, pack(max, i), std::memory_order_release)) {
                        return cleared;
                    }
                }
            }

            range_.compare_exchange_strong(r, Capacity, std::memory_order_relaxed);
        } else {
            // TODO
            //std::abort();
        }

        std::atomic_thread_fence(std::memory_order_release);
        return cleared;
    }

    bool top(T& value) const {
        auto r = range_.load(std::memory_order_acquire);
        if (r == Capacity) return false;

        auto [max, min] = unpack(r);
        // TODO: range can be stale due to erase()
        for (std::size_t index = min; index <= max; ++index) {
            if (get(index)) {
                value = index;
                return true;
            }
        }
        return false;
    }

    bool pop(T& value) {
        auto r = range_.load(std::memory_order_acquire);
    again:
        auto [max, min] = unpack(r);
        if (min < Capacity) {
            // TODO: this is stupid, should iterate words
            std::size_t i = min;
            for(; i < max; ++i) {
                if (get(i)) {
                    if (range_.compare_exchange_strong(r, pack(max, i + 1), std::memory_order_relaxed)) {
                        bitmap_->clear(i);
                        storage_.release(page(i), (uint8_t*)bitmap_ + page(i) * PageSize);

                        value = i;
                        std::atomic_thread_fence(std::memory_order_release);
                        return true;
                    }

                    goto again;
                }
            }

            if (range_.compare_exchange_strong(r, Capacity, std::memory_order_relaxed)) {
                assert(i == max);
                if (get(i)) {
                    bitmap_->clear(i);
                    storage_.release(page(i), (uint8_t*)bitmap_ + page(i) * PageSize);

                    value = i;
                    std::atomic_thread_fence(std::memory_order_release);
                    return true;
                }

                std::atomic_thread_fence(std::memory_order_release);
            } else {
                goto again;
            }
        }

        return false;
    }

    bool get(T value) const {
        return bitmap_->get(value);
    }

private:
    static std::size_t page(std::size_t index) {
        assert(index < Capacity);
        return index / (PageSize * 8);
    }

    static std::tuple< uint32_t, uint32_t > unpack(uint64_t range) {
        return { range >> 32, range };
    }

    static uint64_t pack(uint32_t max, uint32_t min) {
        return ((uint64_t)max << 32) | min;
    }

    detail::elastic_storage< sizeof(T), PageCount, PageSize > storage_;

    uint8_t* mmap_;
    detail::atomic_bitset<Capacity>* bitmap_;
    std::atomic<uint64_t> range_;
};

}
