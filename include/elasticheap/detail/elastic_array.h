//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/utils.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <sys/mman.h>

namespace elasticheap::detail {

template< typename T, std::size_t Size, std::size_t PageSize > struct elastic_array {
    static constexpr std::size_t MmapSize = (sizeof(T) * Size + PageSize - 1) & ~(PageSize - 1);

    using counter_type =
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint8_t>::max(), uint8_t,
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint16_t>::max(), uint16_t,
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint32_t>::max(), uint32_t,
        uint64_t
    >>>;

    elastic_array(void* memory) {
        memory_ = (T*)align<PageSize>(memory);
    }

    T* acquire(std::size_t i) {
        assert(i < Size);
        if (page_refs_[page(i)]++ == 0) {
            auto ptr = &memory_[i];
            if (mprotect(mask<PageSize>(&memory_[i]), PageSize, PROT_READ | PROT_WRITE) != 0)
                __failure("mprotect");
        }

        return &memory_[i];
    }

    void release(T* ptr) {
        release(get_index(ptr));
    }

    void release(std::size_t i) {
        assert(i < Size);
        assert(page_refs_[page(i)] > 0);
        if (--page_refs_[page(i)] == 0) {
            auto ptr = mask<PageSize>(&memory_[i]);
            if (madvise(mask<PageSize>(&memory_[i]), PageSize, MADV_DONTNEED) != 0)
                __failure("madvise");
        }
    }

    std::size_t page(std::size_t i) {
        assert(i < Size);
        return i * sizeof(T) / PageSize;
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
    std::array<counter_type, (sizeof(T) * Size + PageSize - 1) / PageSize > page_refs_ = {0};
    void* mmap_ = 0;
    T* memory_ = 0;
};

}
