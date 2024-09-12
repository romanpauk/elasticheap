//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/utils.h>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>

#include <sys/mman.h>

namespace elasticheap::detail {

template< typename T, std::size_t Size, std::size_t PageSize > struct elastic_atomic_array {
    static constexpr std::size_t MmapSize = (sizeof(T) * Size + PageSize - 1) & ~(PageSize - 1);
    static constexpr std::size_t PageCount = (sizeof(T) * Size + PageSize - 1) / PageSize;

    using counter_type =
        std::conditional_t< PageSize / sizeof(T) < std::numeric_limits<uint8_t>::max()  / 2, uint8_t,
        std::conditional_t< PageSize / sizeof(T) < std::numeric_limits<uint16_t>::max() / 2, uint16_t,
        std::conditional_t< PageSize / sizeof(T) < std::numeric_limits<uint32_t>::max() / 2, uint32_t,
        uint64_t
    >>>;

    static_assert(PageSize / sizeof(T) < std::numeric_limits<uint64_t>::max() / 2);

    static constexpr counter_type CounterMappedBit = counter_type{1} << (sizeof(counter_type)*8 - 1);

    elastic_atomic_array(void* memory) {
        memory_ = (T*)align<PageSize>(memory);
    }

    T* acquire(std::size_t i) {
        assert(i < Size);

        auto& counter = page_refs_[page(i)];
        auto state = counter.fetch_add(1, std::memory_order_relaxed);
        if (!(state & CounterMappedBit)) {
            std::lock_guard< std::mutex > lock(page_locks_[page(i)]);
            state = counter.load(std::memory_order_relaxed);
            if (!(state & CounterMappedBit)) {
                auto ptr = &memory_[i];
                if (mprotect(mask<PageSize>(&memory_[i]), PageSize, PROT_READ | PROT_WRITE) != 0)
                    __failure("mprotect");
                counter.fetch_or(CounterMappedBit, std::memory_order_relaxed);
            }
        }

        return &memory_[i];
    }

    void release(T* ptr) {
        release(get_index(ptr));
    }

    void release(std::size_t i) {
        assert(i < Size);
        assert(page_refs_[page(i)] > 0);

        auto& counter = page_refs_[page(i)];
        auto state = counter.fetch_sub(1, std::memory_order_relaxed);
        if ((state & ~CounterMappedBit) == 1) {
            std::lock_guard< std::mutex > lock(page_locks_[page(i)]);
            state = counter.load(std::memory_order_relaxed);
            if ((state & ~CounterMappedBit) == 0) {
                auto ptr = mask<PageSize>(&memory_[i]);
                if (madvise(mask<PageSize>(&memory_[i]), PageSize, MADV_DONTNEED) != 0)
                    __failure("madvise");
                counter.fetch_and(static_cast<counter_type>(~CounterMappedBit), std::memory_order_relaxed);
            }
        }
    }

    uint32_t get_index(T* desc) {
        auto index = desc - memory_;
        assert(index < Size);
        return index;
    }

    T* get(uint32_t index) {
        assert(index < Size);
        return memory_ + index;
    }

private:
    std::size_t page(std::size_t i) {
        assert(i < Size);
        return i * sizeof(T) / PageSize;
    }

    // TODO: this should use per-cpu locks
    std::array< std::mutex, PageCount > page_locks_;
    std::array< std::atomic< counter_type >, PageCount > page_refs_ = {0};
    void* mmap_ = 0;
    T* memory_ = 0;
};

}

