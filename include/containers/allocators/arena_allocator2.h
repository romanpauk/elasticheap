//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <cstdlib>
#include <memory>
#include <limits>

#if defined(_WIN32)
#include <immintrin.h>
#endif

#include <sys/mman.h>
#include <stdio.h>

namespace containers {

template< typename T, std::size_t ArenaSize > class arena2 { 
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

    static constexpr std::size_t ArenaElements = (ArenaSize - 24 - 4)/(sizeof(T) + 2);
    static_assert(ArenaElements <= std::numeric_limits<uint16_t>::max());

public:
    const uint8_t* begin_;
    uint8_t* ptr_;
    uint8_t* end_;
    uint32_t  free_list_size_;
    uint16_t  free_list_[ArenaElements];

    void* allocate() {
        if (free_list_size_) {
            uint16_t index = free_list_[--free_list_size_];
            return (uint8_t*)begin_ + index * sizeof(T);
        }

        uint8_t* ptr = ptr_;
        if (ptr + sizeof(T) > end_) {
            return 0;
        }

        assert(((uintptr_t)ptr & (alignof(T) - 1)) == 0);
        ptr_ += sizeof(T);
        return ptr;
    }
    
    void deallocate(void* ptr) {
        if ((uint8_t*)ptr + sizeof(T) == ptr_) {
            ptr_ -= sizeof(T);
        } else {
            assert((uintptr_t)begin_ <= (uintptr_t)ptr && (uintptr_t)ptr < (uintptr_t)end_);
            const size_t index = ((uintptr_t)ptr - (uintptr_t)begin_) / sizeof(T);
            assert(free_list_size_ < ArenaElements);
            free_list_[free_list_size_++] = index;
        }
    }
};

template< std::size_t ArenaSize, std::size_t MaxSize = 1ull << 30 > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static constexpr std::size_t ArenaCount = (ArenaSize - 16)/(ArenaSize + 8);
    
    struct memory {
        uint64_t size_ = 0;
        uint64_t free_list_size_ = 0;
        uintptr_t free_list_[ArenaSize];
    };
    
    memory* memory_;
    
    arena_manager() {
        memory_ = (memory*)mmap(0, MaxSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory_ == MAP_FAILED)
            std::abort();

        memory_->size_ = 0;
        memory_->free_list_size_ = 0;
    }

    ~arena_manager() {
        munmap(memory_, MaxSize);
    }

    uintptr_t allocate_arena() {
        if (memory_->free_list_size_) {
            return memory_->free_list_[--memory_->free_list_size_];
        }

        return ((reinterpret_cast<uintptr_t>(memory_) + sizeof(memory) + ArenaSize - 1) & ~(ArenaSize - 1)) + memory_->size_++ * ArenaSize;
    }

    void deallocate_arena(uintptr_t ptr) {
        assert(memory_->free_list_size_ < ArenaCount);
        memory_->free_list_[memory_->free_list_size_++] = ptr;
    }

    uintptr_t get_arena(void* ptr) {
        return (uintptr_t)ptr & ~(ArenaSize - 1);
    }
};

template <typename T > class arena_allocator2 {
    template <typename U> friend class arena_allocator2;
    
    static constexpr std::size_t ArenaSize = 1 << 18; // 262k
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    
    arena_manager<ArenaSize> arena_manager_;
    arena2<T, ArenaSize>* arena_;

    arena2<T, ArenaSize>* allocate_arena() {
        auto arena = reinterpret_cast<arena2<T, ArenaSize>*>(arena_manager_.allocate_arena());
        arena->begin_ = arena->ptr_ = reinterpret_cast<uint8_t*>(arena) + sizeof(*arena);
        arena->end_ = reinterpret_cast<uint8_t*>(arena) + ArenaSize;
        arena->free_list_size_ = 0;
        return arena;
    }
    
public:
    using value_type    = T;

    arena_allocator2() noexcept {
        arena_ = allocate_arena();
    }
    
    value_type* allocate(std::size_t n) {
        assert(n == 1);
        (void)n;
        auto ptr = reinterpret_cast<value_type*>(arena_->allocate());
        if (!ptr) {
            arena_ = allocate_arena();
            ptr = reinterpret_cast<value_type*>(arena_->allocate());
        }
        return ptr;
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        assert(n == 1);
        (void)n;
        auto arena = reinterpret_cast<arena2<T, ArenaSize>*>(arena_manager_.get_arena(ptr));
        arena->deallocate(ptr);
    }
};

template <typename T, typename U>
bool operator == (const arena_allocator2<T>& lhs, const arena_allocator2<U>& rhs) noexcept {
    return lhs.arena_ = rhs.arena_;
}

template <typename T, typename U>
bool operator != (const arena_allocator2<T>& x, const arena_allocator2<U>& y) noexcept {
    return !(x == y);
}

}