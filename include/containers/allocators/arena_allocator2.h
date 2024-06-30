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

//#define STATS
#define PAGE_MANAGER_ELASTIC
//#define ARENA_MANAGER_ELASTIC
#define ARENA_ALLOCATOR_BASE_HEAP
#define ARENA_ALLOCATOR_BASE_ELASTIC

namespace containers {

struct allocator_stats {
    std::size_t pages_allocated = 0;
    std::size_t pages_deallocated_heap_size = 0;
    std::size_t pages_deallocated_heap_commited = 0;
    std::size_t arenas_allocated = 0;
    std::size_t arenas_deallocated_heap_size = 0;
};

static allocator_stats stats;

void print_stats() {
    fprintf(stderr, "stats: pages_allocated %lu, pages_deallocated_heap_size %lu, pages_deallocated_heap_commited %lu, arenas_allocated %lu, arenas_deallocated_heap_size %lu\n",
        stats.pages_allocated, stats.pages_deallocated_heap_size, stats.pages_deallocated_heap_commited, stats.arenas_allocated, stats.arenas_deallocated_heap_size
    );
}

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
    uint32_t size_;
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
        free_list_size_ = size_ = 0;
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
        ++size_;
        return ptr;
    }
    
    void deallocate(void* ptr) {
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / Size;
        assert(index < Count);
        assert(free_list_size_ < Count);
        free_list_[free_list_size_++] = index;
        --size_;
    }

    static constexpr std::size_t capacity() { return Count; }
    std::size_t size() const { return size_; }

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

    void erase(T value) {
        assert(!empty());
        for(size_t i = 0; i < size_; ++i) {
            if (values_[i] == value) {
                values_[i] = values_[--size_];
                std::make_heap(values_, values_ + size_, Compare{});
                return;
            }
        }

        assert(false);
    }

    std::size_t size() const { return size_; }

private:
    std::size_t size_ = 0;
    T values_[Capacity];
};

template< typename T, std::size_t Size, std::size_t PageSize = 4096 > struct elastic_array {
    using size_type = uint32_t;
    static_assert(sizeof(T) * Size < std::numeric_limits<size_type>::max());

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

    T& operator[](size_type n) {
        assert(n < size_);
        return *(memory_ + n);
    }

    void emplace_back(T value) {
        grow();
        *(memory_ + size_++) = value;
    }

    void emplace_back(T* begin, T* end) {
        grow(end - begin);
        while(begin != end) {
            *(memory_ + size_++) = *begin++;
        }
    }

    void pop_back() {
        assert(!empty());
        --size_;
        shrink();
    }

    size_type size() const { return size_; }
    size_type size_commited() const { return size_commited_; }

private:
    void grow(size_type n = 1) {
        if (size_ + n > size_commited_) {
            mprotect((uint8_t*)memory_ + size_commited_ * sizeof(T), PageSize, PROT_READ | PROT_WRITE);
            size_commited_ += PageSize/sizeof(T);
        }
    }

    void shrink() {
        if (size_ + PageSize/sizeof(T) < size_commited_) {
            mprotect((uint8_t*)memory_ + size_commited_ * sizeof(T) - PageSize/sizeof(T), PageSize, PROT_NONE);
            size_commited_ -= PageSize/sizeof(T);
        }
    }

    T* memory_ = 0;
    size_type size_ = 0;
    size_type size_commited_ = 0;
};

template< typename T, std::size_t Size, typename Compare = std::greater<> > struct elastic_heap {
    void push(T value) {
        values_.emplace_back(value);
        std::push_heap(values_.begin(), values_.end(), Compare{}); 
    }

    template< size_t N > void push(const std::array<T, N>& values) {
        values_.emplace_back(values.begin(), values.end());
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

    void erase(T value) {
        for(size_t i = 0; i < values_.size(); ++i) {
            if (values_[i] == value) {
                values_[i] = values_.back();
                values_.pop_back();
                std::make_heap(values_.begin(), values_.end(), Compare{});
                return;
            }
        }

        assert(false);
    }

    auto size() const { return values_.size(); }
    auto size_commited() const { return values_.size_commited(); }

private:
    elastic_array<T, Size> values_;
};

enum PageState {
    Deallocated = 0,
    Allocated = 1,
    Full = 2,
};

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t MmapSize = MaxSize + PageSize - 1;
    static constexpr std::size_t PageCount = MaxSize / PageSize;
    static_assert(PageCount <= std::numeric_limits< uint32_t >::max());

    struct page_metadata {
        uint8_t state; // TODO: get rid of allocated, allocate_arena() needs to change
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
    #if defined(STATS)
        ++stats.pages_allocated;
        stats.pages_deallocated_heap_size = deallocated_pages_.size();
        stats.pages_deallocated_heap_commited = deallocated_pages_.size_commited();
    #endif
        return ptr;
    }

    void deallocate_page(void* ptr) {
        assert(is_page_valid(ptr));
    
        //mprotect(ptr, PageSize, PROT_NONE);
        madvise(ptr, PageSize, MADV_DONTNEED);

        std::lock_guard lock(mutex_);
        deallocated_pages_.push(get_page_index(ptr));
    #if defined(STATS)
        --stats.pages_allocated;
        stats.pages_deallocated_heap_size = deallocated_pages_.size();
        stats.pages_deallocated_heap_commited = deallocated_pages_.size_commited();
    #endif
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

#if defined(PAGE_MANAGER_ELASTIC)
    elastic_heap< uint32_t, PageCount > deallocated_pages_;
#else
    heap< uint32_t, PageCount > deallocated_pages_;
#endif
    
    page_metadata metadata_[PageCount];
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static constexpr std::size_t ArenaCount = MaxSize/ArenaSize;
    
    static_assert(ArenaCount <= std::numeric_limits<uint32_t>::max());

    void* allocate_arena() {
        void* ptr = page_manager_.allocate_page();
        auto& metadata = page_manager_.get_page_metadata(ptr);
        metadata.state = Allocated;
        assert(is_arena_valid(ptr));
    #if defined(STATS)
        ++stats.arenas_allocated;
    #endif
        return ptr;
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
        metadata.state = Deallocated;
        page_manager_.deallocate_page(page);
    
    #if defined(STATS)
        --stats.arenas_allocated;
    #endif
    }

    uint8_t get_arena_state(void* ptr) {
        assert(is_arena_valid(ptr));
        void* page = page_manager_.get_page(ptr);
        auto& metadata = page_manager_.get_page_metadata(page);
        return metadata.state;
    }

private:
    
    bool is_arena_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, ArenaSize, page_manager_.begin(), page_manager_.end()));
        assert(is_ptr_aligned(ptr, ArenaSize));
        return true;
    }

    page_manager< PageSize, MaxSize > page_manager_;

#if defined(ARENA_MANAGER_ELASTIC)
    elastic_heap< uint32_t, ArenaCount > arena_cache_;
#else
    heap< uint32_t, ArenaCount > arena_cache_;
#endif
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > class arena_allocator_base {
    static_assert((PageSize & (PageSize - 1)) == 0);
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static_assert((MaxSize & (MaxSize - 1)) == 0);

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

    template< size_t SizeClass > void* allocate_impl() {
    again:
        auto arena = get_arena<SizeClass>();
        if (arena_manager_.get_arena_state(arena) == Allocated) {
            auto ptr = arena->allocate();
            if (ptr)
                return ptr;

            assert(arena->size() == arena->capacity());
        }

        pop_arena<SizeClass>();

        if(get_size<SizeClass>())
            goto again;

        return allocate_arena<SizeClass>()->allocate();
    }

    template< size_t SizeClass > void deallocate_impl(void* ptr) noexcept {
        auto arena = get_arena<SizeClass>(ptr);
        arena->deallocate(ptr);
        if(arena->size() == 0) {
            deallocate_arena<SizeClass>(arena);
        } else if(arena->size() == arena->capacity() - 1) {
            push_arena<SizeClass>(arena);
        }
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* get_arena() {
        auto offset = size_class_offset(SizeClass);
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        if (get_size<SizeClass>() == 0)
            allocate_arena<SizeClass>();
        return (arena2<ArenaSize, SizeClass, 8>*)classes_[offset].top();
    #else
        return (arena2<ArenaSize, SizeClass, 8>*)classes_[offset];
    #endif
    }

    template< size_t SizeClass > size_t get_size() {
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        auto offset = size_class_offset(SizeClass);
        return classes_[offset].size();
    #else
        return 0;
    #endif
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* get_arena(void* ptr) {
        return (arena2<ArenaSize, SizeClass, 8>*)arena_manager_.get_arena(ptr);
    }

    template< size_t SizeClass > arena2<ArenaSize, SizeClass, 8>* allocate_arena() {
        auto offset = size_class_offset(SizeClass);
        void* ptr = arena_manager_.allocate_arena();
        auto* arena = new (ptr) arena2<ArenaSize, SizeClass, 8>;
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        classes_[offset].push(arena);
    #else
        classes_[offset] = arena;
    #endif
        return arena;
    }

    template< size_t SizeClass > void deallocate_arena(void* ptr) {
        auto offset = size_class_offset(SizeClass);
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        if (classes_[offset].top() != ptr) {
            arena_manager_.deallocate_arena(ptr);
        }
    #else
        if (classes_[offset] != ptr) {
            arena_manager_.deallocate_arena(ptr);
        }
    #endif
    }

    template< size_t SizeClass > void pop_arena() {
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        auto offset = size_class_offset(SizeClass);
        classes_[offset].pop();
    #endif
    }
    
    template< size_t SizeClass > void push_arena(void* ptr) {
        (void)ptr;
    #if defined(ARENA_ALLOCATOR_BASE_HEAP)
        auto offset = size_class_offset(SizeClass);
        classes_[offset].push(ptr);
    #endif
    }

#if defined(ARENA_ALLOCATOR_BASE_HEAP)
#if defined(ARENA_ALLOCATOR_BASE_ELASTIC)
    static std::array<elastic_heap<void*, MaxSize/ArenaSize>, 23> classes_;
#else
    static std::array<heap<void*, MaxSize/ArenaSize>, 23> classes_;    
#endif
#else
    static std::array<void*, 23> classes_;
#endif
    static arena_manager<PageSize, ArenaSize, MaxSize> arena_manager_;
};

#if defined(ARENA_ALLOCATOR_BASE_HEAP)
#if defined(ARENA_ALLOCATOR_BASE_ELASTIC)
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<elastic_heap<void*, MaxSize/ArenaSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::classes_;
#else
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<heap<void*, MaxSize/ArenaSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::classes_;
#endif
#else
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<void*, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::classes_;
#endif

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> arena_manager<PageSize, ArenaSize, MaxSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::arena_manager_;

template <typename T > class arena_allocator2
    : public arena_allocator_base< 1<<21, 1<<19, 1ull<<40> 
{
    template <typename U> friend class arena_allocator2;
    
public:
    using value_type    = T;

    arena_allocator2() noexcept {
        if (!get_size<size_class<T>()>())
            allocate_arena<size_class<T>()>();
    }
    
    value_type* allocate(std::size_t n) {
        assert(n == 1);
        (void)n;
        return reinterpret_cast< value_type* >(allocate_impl<size_class<T>()>());
    }
    
    void deallocate(value_type* ptr, std::size_t n) noexcept {
        assert(n == 1);
        (void)n;
        deallocate_impl<size_class<T>()>(ptr);
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