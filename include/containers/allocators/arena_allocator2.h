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
#include <stdexcept>

#if defined(_WIN32)
#include <immintrin.h>
#endif

#include <sys/mman.h>
#include <stdio.h>

#if 0
#undef assert
#define assert(exp) \
    do { \
        if(!(exp)) { \
            fprintf(stderr, "Assertion failed: " #exp "\n"); \
            std::abort(); \
        } \
    } while(0);
#endif

namespace containers {

inline bool is_ptr_aligned(void* ptr, std::size_t alignment) {
    return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

inline bool is_ptr_in_range(void* ptr, std::size_t size, void* begin, void* end) {
    return (uintptr_t)ptr >= (uintptr_t)begin && (uintptr_t)ptr + size <= (uintptr_t)end;
}

struct arena2_metadata {
    uint8_t* begin_;
    uint8_t* ptr_;
    uint8_t* end_;
    uint32_t free_list_size_;
};

template< std::size_t ArenaSize, std::size_t Size, std::size_t Alignment > class arena2
    : public arena2_metadata
{ 
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

    static constexpr std::size_t Count = (ArenaSize - sizeof(arena2_metadata))/(Size + 2);
    static_assert(Count <= std::numeric_limits<uint16_t>::max());

public:
    uint16_t  free_list_[Count];

    arena2() = default;

    arena2(uint8_t*, size_t) {
        begin_ = (uint8_t*)this + sizeof(*this);
        ptr_ = begin_;
        end_ = (uint8_t*)this + ArenaSize;
        free_list_size_ = 0;
    }

    void* allocate() {
        uint8_t* ptr = 0;
        if (free_list_size_) {
            uint16_t index = free_list_[--free_list_size_];
            ptr = begin_ + index * Size;
        } else {
            ptr = ptr_;
            if (ptr + Size > end_)
                return 0;

            ptr_ += Size;
        }

        assert(is_ptr_in_range(ptr, Size, begin_, end_)); 
        assert(is_ptr_aligned(ptr, Alignment));
           
        return ptr;
    }
    
    bool deallocate(void* ptr) {
        assert(is_ptr_in_range(ptr, Size, begin_, end_));
        assert(is_ptr_aligned(ptr, Alignment));
        
        size_t index = ((uint8_t*)ptr - begin_) / Size;
        assert(index < Count);
        assert(free_list_size_ < Count);
        free_list_[free_list_size_++] = index;
        return free_list_size_ == Count;
    }
};

template< std::size_t ArenaSize, std::size_t MaxSize = 1ull << 32 > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static constexpr std::size_t ArenaCount = (MaxSize - 16 - ArenaSize + 1)/(ArenaSize + 8);
    
    struct memory {
        uint64_t size_ = 0;
        uint64_t free_list_size_ = 0;
        void* free_list_[ArenaCount];
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

    void* allocate_arena() {
        void* ptr = 0;
        if (memory_->free_list_size_) {
            ptr = memory_->free_list_[--memory_->free_list_size_];
        } else {
            if (memory_->size_ == ArenaCount)
                return nullptr;
            ptr = (void*)((((uintptr_t)(memory_) + sizeof(memory) + ArenaSize - 1) & ~(ArenaSize - 1)) + memory_->size_++ * ArenaSize);
        }

        assert(is_ptr_in_range(ptr, ArenaSize, memory_ + 1, (uint8_t*)memory_ + MaxSize));
        assert(is_ptr_aligned(ptr, ArenaSize));
        return ptr;
    }

    void deallocate_arena(void* ptr) {
        assert(is_ptr_in_range(ptr, ArenaSize, memory_ + 1, (uint8_t*)memory_ + MaxSize));
        assert(is_ptr_aligned(ptr, ArenaSize));
        assert(memory_->free_list_size_ < ArenaCount);

        memory_->free_list_[memory_->free_list_size_++] = ptr;
    }

    void* get_arena(void* ptr) {
        assert(is_ptr_in_range(ptr, 1, memory_ + 1, (uint8_t*)memory_ + MaxSize));
        
        return reinterpret_cast<void*>((uintptr_t)ptr & ~(ArenaSize - 1));
    }
};

template <typename T > class arena_allocator2 {
    template <typename U> friend class arena_allocator2;
    
    static constexpr std::size_t ArenaSize = 1 << 18; // 18: 262k
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    
    arena_manager<ArenaSize> arena_manager_;
    using arena_type = arena2<ArenaSize, sizeof(T), alignof(T) >;
    arena_type* arena_;

    arena_type* allocate_arena() {
        auto arena = reinterpret_cast<arena_type*>(arena_manager_.allocate_arena());
        if (!arena) {
            throw std::runtime_error("allocate_arena()");
            return nullptr;
        }
        
        arena->begin_ = arena->ptr_ = reinterpret_cast<uint8_t*>(arena) + sizeof(*arena);
        arena->end_ = reinterpret_cast<uint8_t*>(arena) + ArenaSize;
        arena->free_list_size_ = 0;
        return arena;
    }
    
public:
    using value_type    = T;

    arena_allocator2() noexcept {
        arena_ = allocate_arena();
        assert(arena_);
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
        auto arena = reinterpret_cast<arena_type*>(arena_manager_.get_arena(ptr));
        if(arena->deallocate(ptr)) {
            if (arena_ == arena)
                arena_ = allocate_arena();
            arena_manager_.deallocate_arena(arena);
        }
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