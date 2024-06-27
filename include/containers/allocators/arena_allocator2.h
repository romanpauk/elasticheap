//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <array>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
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

    arena2() {
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

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t PageCount = MaxSize / PageSize;

    page_manager() {
        memory_ = (uint8_t*)mmap(0, MaxSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory_ == MAP_FAILED)
            std::abort();
    }

    ~page_manager() {
        munmap(memory_, MaxSize);
    }

    void* allocate_page() {
        void* ptr = 0;
        {
            std::lock_guard lock(mutex_);
            
            if (free_list_size_) {
                ptr = free_list_[--free_list_size_];
            } else {
                ptr = (void*)((((uintptr_t)(memory_) + PageSize - 1) & ~(PageSize - 1)) + size_++ * PageSize);
            }
        }

        assert(is_ptr_in_range(ptr, PageSize, begin(), end()));
        assert(is_ptr_aligned(ptr, PageSize));

        mprotect(ptr, PageSize, PROT_READ | PROT_WRITE);
        return ptr;
    }

    void deallocate_page(void* ptr) {
        assert(is_ptr_in_range(ptr, PageSize, begin(), end()));
        assert(is_ptr_aligned(ptr, PageSize));
        
        mprotect(ptr, PageSize, PROT_NONE);

        std::lock_guard lock(mutex_);
        assert(free_list_size_ < PageCount);
        free_list_[free_list_size_++] = ptr;
    }

    void* begin() { return memory_; }
    void* end() { return (uint8_t*)memory_ + MaxSize; }

    std::mutex mutex_;
    uint64_t size_ = 0;
    uint64_t free_list_size_ = 0;
    void* memory_ = 0;
    void* free_list_[PageCount];
};

template< std::size_t ArenaSize, std::size_t MaxSize = 1ull << 32 > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static constexpr std::size_t PageSize = 1<<21;
    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);
    
    struct free_stack {
        static constexpr std::size_t capacity = (PageSize - sizeof(16))/sizeof(void*); 
        free_stack* next_ = 0;
        uint64_t size_ = 0;
        void* data_[];
    };
    
    //static_assert(sizeof(free_stack) <= PageSize);

    page_manager<PageSize, MaxSize> page_manager_;
    free_stack* free_stack_;

    void* arena_page_;
    std::size_t arena_page_size_;

    free_stack* allocate_free_stack() {
        auto ptr = (free_stack*)page_manager_.allocate_page();
        new (ptr) free_stack();
        return ptr;
    }

    void free_stack_push(void* ptr) {
        if (free_stack_->size_ == free_stack::capacity) {
            auto stack = allocate_free_stack();
            stack->next_ = free_stack_;
            free_stack_ = stack;
        }

        free_stack_->data_[free_stack_->size_++] = ptr;
    }

    void* free_stack_pop() {
        if (free_stack_->size_ == 0) {
            if (free_stack_->next_) {
                auto next = free_stack_->next_;
                page_manager_.deallocate_page(free_stack_);
                free_stack_ = next;
            } else {
                return nullptr;
            }
        }

        return free_stack_->data_[--free_stack_->size_];
    }

    arena_manager() {
        free_stack_ = allocate_free_stack();
        arena_page_ = page_manager_.allocate_page();
        arena_page_size_ = 0;
    }

    ~arena_manager() {
        do {
            auto next = free_stack_->next_;
            page_manager_.deallocate_page(free_stack_);
            free_stack_ = next;
        } while(free_stack_);

        page_manager_.deallocate_page(arena_page_);
    }

    void* allocate_arena() {
        void* ptr = free_stack_pop();
        if (!ptr) {
            if (arena_page_size_ == PageArenaCount) {
                arena_page_ = page_manager_.allocate_page();
                arena_page_size_ = 0;
            }

            ptr = (void*)((((uintptr_t)(arena_page_) + ArenaSize - 1) & ~(ArenaSize - 1)) + arena_page_size_++ * ArenaSize);
            assert(is_ptr_in_range(ptr, ArenaSize, arena_page_, (uint8_t*)arena_page_ + PageSize));
        }

        assert(is_ptr_aligned(ptr, ArenaSize));
        return ptr;
    }

    void deallocate_arena(void* ptr) {
        assert(is_ptr_in_range(ptr, ArenaSize, page_manager_.begin(), page_manager_.end()));
        assert(is_ptr_aligned(ptr, ArenaSize));
        free_stack_push(ptr);
    }

    void* get_arena(void* ptr) {
        assert(is_ptr_in_range(ptr, 1, page_manager_.begin(), page_manager_.end()));
        return reinterpret_cast<void*>((uintptr_t)ptr & ~(ArenaSize - 1));
    }
};

class arena_allocator_base {
    static constexpr std::size_t ArenaSize = 1 << 18; // 18: 262k
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

protected:
    static arena_manager<ArenaSize> arena_manager_;
    
    static constexpr uint32_t round_up(uint32_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    static constexpr size_t log2(size_t n) { return ((n<2) ? 1 : 1 + log2(n/2)); }

    static constexpr size_t size_class(size_t n) { return round_up(std::max(n, 8lu)); }

    template< typename T > static constexpr size_t size_class() { 
        size_t n = round_up(std::max(sizeof(T), 8lu));
        if (n > 8)
            if (n - n/2 >= sizeof(T)) return n - n/2;
        return n;
    }

    static constexpr size_t size_class_offset(size_t n) {
        switch(n) {
        case 8:     return 0;
        case 12:    return 1;
        case 16:    return 2;
        case 24:    return 3;
        case 32:    return 4;
        case 48:    return 5;
        case 64:    return 6;
        case 96:    return 7;
        case 128:   return 8;
        case 224:   return 9;
        case 256:   return 10;
        case 384:   return 11;
        case 512:   return 12;
        case 768:   return 13;
        case 1024:  return 14;
        case 1536:  return 15;
        case 2048:  return 16;
        case 3072:  return 17;
        case 4096:  return 18;
        case 6144:  return 19;
        case 8192:  return 20;
        case 12288: return 21;
        case 16384: return 22;
        default:
            std::abort();
        }
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* get_arena() {
        auto offset = size_class_offset(SizeClass);
        return (arena2<ArenaSize, SizeClass, 8>*)classes_[offset];
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* get_arena(void* ptr) {
        return (arena2<ArenaSize, SizeClass, 8>*)arena_manager_.get_arena(ptr);
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* allocate_arena() {
        auto offset = size_class_offset(SizeClass);
        void* ptr = arena_manager_.allocate_arena();
        auto* arena = new (ptr) arena2<ArenaSize, SizeClass, 8>;
        classes_[offset] = arena;
        return arena;
    }

    template< size_t SizeClass > void deallocate_arena(void* ptr) {
        auto offset = size_class_offset(SizeClass);
        if (classes_[offset] == ptr)
            classes_[offset] = allocate_arena<SizeClass>();
        arena_manager_.deallocate_arena(ptr);
    }

    static std::array<void*, 23> classes_;
};

std::array<void*, 23> arena_allocator_base::classes_;
arena_manager<1<<18> arena_allocator_base::arena_manager_;

//thread_local arena2<1<<18, 8, 8 >* arena_allocator_base::arena_;

template <typename T > class arena_allocator2: public arena_allocator_base {
    template <typename U> friend class arena_allocator2;
    
public:
    using value_type    = T;

    arena_allocator2() noexcept {
        allocate_arena<size_class<T>()>();
    }
    
    value_type* allocate(std::size_t n) {
        assert(n == 1);
        (void)n;
        auto ptr = reinterpret_cast<value_type*>(get_arena<size_class<T>()>()->allocate());
        if (!ptr) {
            ptr = reinterpret_cast<value_type*>(allocate_arena<size_class<T>()>()->allocate());
        }
        return ptr;
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        assert(n == 1);
        (void)n;
        auto arena = get_arena<size_class<T>()>(ptr);
        if(arena->deallocate(ptr)) {
            deallocate_arena<size_class<T>()>(arena);
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