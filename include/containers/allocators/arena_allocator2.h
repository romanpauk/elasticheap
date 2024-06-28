//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <deque>
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

template < std::size_t Alignment, typename T > T* align(T* ptr) {
    static_assert((Alignment & (Alignment - 1)) == 0);
    return (T*)(((uintptr_t)ptr + Alignment - 1) & ~(Alignment - 1));
}

template < std::size_t Alignment, typename T > T* mask(T* ptr) {
    static_assert((Alignment & (Alignment - 1)) == 0);
    return (T*)((uintptr_t)ptr & ~(Alignment - 1));
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

    uint16_t  free_list_[Count];
    
public:
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

        assert(is_ptr_valid(ptr));
        return ptr;
    }
    
    bool deallocate(void* ptr) {
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / Size;
        assert(index < Count);
        assert(free_list_size_ < Count);
        free_list_[free_list_size_++] = index;
        return free_list_size_ == Count;
    }

private:
    bool is_ptr_valid(void* ptr) {
        assert(is_ptr_in_range(ptr, Size, begin_, end_));
        assert(is_ptr_aligned(ptr, Alignment));
        return true;
    }
};

template< typename T, std::size_t Capacity, typename Compare = std::greater<> > struct heap {
    void push(T value) {
        assert(size_ < Capacity);
        values_[size_++] = value;
        std::push_heap(values_, values_ + size_, Compare{}); 
    }

    template< size_t N > void push(const std::array<T, N>& values) {
        for(size_t i = 0; i < values.size(); ++i) {
            assert(size_ < Capacity);
            values_[size_++] = values[i];
        }
        std::make_heap(values_, values_ + size_, Compare{}); 
    }

    bool empty() const {
        return size_ == 0;
    }

    T pop() {
        std::pop_heap(values_, values_ + size_, Compare{});
        return values_[--size_];
    }

    T& top() {
        assert(!empty());
        return values_[0];
    }

private:
    std::size_t size_ = 0;
    T values_[Capacity];
};

template< typename T, std::size_t Size, std::size_t PageSize = 4096 > struct elastic_array {
    elastic_array() {
        memory_ = (T*)mmap(0, sizeof(T) * Size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory_ == MAP_FAILED)
            std::abort();
    }

    ~elastic_array() {
        munmap(memory_, sizeof(T) * Size);
    }

    T* begin() { return memory_; }
    T* end() { return memory_ + size_; }
    bool empty() const { return size_ == 0; }
    
    T& back() {
        assert(!empty());
        return *(memory_ + size_ - 1);
    }

    void emplace_back(T value) {
        grow();
        *(memory_ + size_) = value;
        ++size_;
    }

    void pop_back() {
        assert(!empty());
        --size_;
        shrink();
    }

private:
    void grow() {
        if ((size_ * sizeof(T) / PageSize) + 1 > pages_) {
            mprotect((uint8_t*)memory_ + PageSize * pages_, PageSize, PROT_READ | PROT_WRITE);
            ++pages_;
        }
    }

    void shrink() {
        assert(pages_ > 0);
        if ((size_ * sizeof(T) / PageSize) + 1 < pages_) {
            mprotect((uint8_t*)memory_ + PageSize * (pages_ - 1), PageSize, PROT_NONE);
            --pages_;
        }
    }

    T* memory_ = 0;
    std::size_t size_ = 0;
    std::size_t pages_ = 0;
};

template< typename T, std::size_t Size, typename Compare = std::greater<> > struct elastic_heap {
    void push(T value) {
        values_.emplace_back(value);
        std::push_heap(values_.begin(), values_.end(), Compare{}); 
    }

    template< size_t N > void push(const std::array<T, N>& values) {
        for(size_t i = 0; i < values.size(); ++i) {
            values_.emplace_back(values[i]);
        }
        std::make_heap(values_.begin(), values_.end(), Compare{}); 
    }

    bool empty() const {
        return values_.empty();
    }

    T pop() {
        std::pop_heap(values_.begin(), values_.end(), Compare{});
        T value = values_.back();
        values_.pop_back();
        return value;
    }

    T& top() {
        return values_[0];
    }

private:
    elastic_array<T, Size> values_;
};

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t MmapSize = MaxSize + PageSize - 1;
    static constexpr std::size_t PageCount = MaxSize / PageSize;
    static_assert(PageCount <= std::numeric_limits< uint32_t >::max());

    struct page_metadata {
        bool allocated; // TODO: get rid of allocated, allocate_arena() needs to change
        uint8_t refs;
    };

    page_manager() {
        mmap_ = (uint8_t*)mmap(0, MmapSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mmap_ == MAP_FAILED)
            std::abort();

        memory_ = align<PageSize>(mmap_);
    }

    ~page_manager() {
        munmap(mmap_, MmapSize);
    }

    void* allocate_page() {
        void* ptr = 0;
        {
            std::lock_guard lock(mutex_);

            if (!deallocated_pages_.empty()) {
                ptr = get_page(deallocated_pages_.pop());
            } else {
                if (memory_size_ == PageCount)
                    std::abort();
                ptr = (uint8_t*)memory_ + memory_size_++ * PageSize;
            }
        }

        assert(is_page_valid(ptr));
        mprotect(ptr, PageSize, PROT_READ | PROT_WRITE);
        return ptr;
    }

    void deallocate_page(void* ptr) {
        assert(is_page_valid(ptr));
        mprotect(ptr, PageSize, PROT_NONE);

        std::lock_guard lock(mutex_);
        deallocated_pages_.push(get_page_index(ptr));
    }

    void* get_page(void* ptr) const {
        assert(is_ptr_in_range(ptr, 1, begin(), end()));
        return mask<PageSize>(ptr);
    }

    void* get_page(uint32_t index) const {
        void* ptr = (uint8_t*)memory_ + index * PageSize;
        assert(is_page_valid(ptr));
        return ptr;
    }

    void* begin() const { return memory_; }
    void* end() const { return (uint8_t*)memory_ + PageSize * PageCount; }

    page_metadata& get_page_metadata(void* page) {
        return metadata_[get_page_index(page)];
    }

private:
    uint32_t get_page_index(void* ptr) const {
        assert(is_page_valid(ptr));
        return ((uint8_t*)ptr - (uint8_t*)memory_) / PageSize;
    }

    bool is_page_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, PageSize, begin(), end()));
        assert(is_ptr_aligned(ptr, PageSize));
        return true;
    }

    void* mmap_ = 0;
    void* memory_ = 0;
    uint64_t memory_size_ = 0;
    
    std::mutex mutex_;
    heap< uint32_t, PageCount > deallocated_pages_;
    //elastic_heap< uint32_t, PageCount > deallocated_pages_;
    
    page_metadata metadata_[PageCount];
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);
    static constexpr std::size_t ArenaCount = MaxSize/ArenaSize;
    static constexpr std::size_t PageCount = MaxSize/PageSize;

    static_assert(ArenaCount <= std::numeric_limits<uint32_t>::max());

    void* allocate_arena() {
        while(true) {
            if (arena_cache_.empty())
                allocate_arena_cache();
            
            void* ptr = get_arena(arena_cache_.pop());
            void* page = page_manager_.get_page(ptr);
            auto& metadata = page_manager_.get_page_metadata(page);
            if (metadata.allocated) {
                ++metadata.refs;
                assert(is_arena_valid(ptr));
                return ptr;
            }
        }
    }

    void* get_arena(void* ptr) const {
        assert(is_ptr_in_range(ptr, 1, page_manager_.begin(), page_manager_.end()));
        return mask<ArenaSize>(ptr);
    }

    void* get_arena(uint32_t index) const {
        void* ptr = (uint8_t*)page_manager_.begin() + ArenaSize * index;
        assert(is_arena_valid(ptr));
        return ptr;
    }

    uint32_t get_arena_index(void* ptr) const {
        assert(is_arena_valid(ptr));
        return ((uint8_t*)ptr - (uint8_t*)page_manager_.begin()) / ArenaSize;
    }

    void deallocate_arena(void* ptr) {
        assert(is_arena_valid(ptr));
        void* page = page_manager_.get_page(ptr);
        auto& metadata = page_manager_.get_page_metadata(page);
        if (--metadata.refs == 0) {
            metadata.allocated = false;
            page_manager_.deallocate_page(page);
        } else {
            arena_cache_.push(get_arena_index(ptr));
        }
    }

private:
    void allocate_arena_cache() {
        void* page = page_manager_.allocate_page();
        auto& metadata = page_manager_.get_page_metadata(page);
        metadata.refs = 0;
        metadata.allocated = true;

        std::array< uint32_t, PageArenaCount > arenas;
        for (size_t i = 0; i < arenas.size(); ++i) {
            void* arena = (uint8_t*)page + i * ArenaSize;
            assert(is_arena_valid(arena));
            arenas[i] = get_arena_index(arena);
        }

        arena_cache_.push(arenas);
    }

    bool is_arena_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, ArenaSize, page_manager_.begin(), page_manager_.end()));
        assert(is_ptr_aligned(ptr, ArenaSize));
        return true;
    }

    page_manager< PageSize, MaxSize > page_manager_;
    heap< uint32_t, ArenaCount > arena_cache_;
    //elastic_heap< uint32_t, ArenaCount > arena_cache_;
};

class arena_allocator_base {
    static constexpr std::size_t PageSize = 1<<21;
    static_assert((PageSize & (PageSize - 1)) == 0);

    static constexpr std::size_t ArenaSize = 1 << 18; // 18: 262k    
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);
    
protected:
    
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
    static arena_manager<1<<21, 1<<18, 1ull<<32> arena_manager_;
};

std::array<void*, 23> arena_allocator_base::classes_;
arena_manager<1<<21, 1<<18, 1ull<<32> arena_allocator_base::arena_manager_;

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