//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/bitset.h>
#include <elasticheap/detail/bitset_heap.h>
#include <elasticheap/detail/atomic_bitset_heap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>

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

#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)

//#define STATS
//#define THREADS

namespace elasticheap {

struct allocator_stats {
    std::size_t pages_allocated = 0;
    std::size_t pages_deallocated_heap_size = 0;
    std::size_t pages_deallocated_heap_commited = 0;
    std::size_t arenas_allocated = 0;
    std::size_t arenas_deallocated_heap_size = 0;
};

static allocator_stats stats;

inline void print_stats() {
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

#if defined(THREADS)
static thread_local uint64_t thread_token;
inline uint64_t thread_id() {
    return (uint64_t)&thread_token;
}
#endif

struct arena_metadata {
#if defined(THREADS)
    uint64_t tid_;
#endif
    uint8_t* begin_;
    uint32_t index_;
    uint32_t size_class_;
    uint32_t free_list_size_;
};

template< typename T, std::size_t Size > struct arena_free_list {
    static_assert(Size <= std::numeric_limits<T>::max());

    detail::bitset<Size> bitmap_;

    void push(T value, uint32_t& size) {
        assert(value < Size);
        assert(size < Size);
        bitmap_.set(value);
        ++size;
    }

    T pop(uint32_t& size) {
        assert(size);
        --size;
        return bitmap_.pop_first();
    }
};

template< typename T, std::size_t Size > struct arena_free_list2 {
    static_assert(Size <= std::numeric_limits<T>::max());

    std::array< T, Size > values_;

    void push(T value, uint32_t& size) {
        assert(value < Size);
        assert(size < Size);
        values_[size++] = value;
    }

    T pop(uint32_t& size) {
        assert(size);
        return values_[--size];
    }
};

template< typename T, std::size_t Size > struct arena_free_list3 {
    static_assert(Size <= 64 * 256);
    
    detail::bitset< 256 > index_;
    std::array< detail::bitset<64>, 256 > bitmap_;

    void push(uint16_t value, uint32_t& size) {
        assert(value < Size);
        assert(size < Size);
        index_.set(value >> 6);
        bitmap_[value >> 6].set(value & 63);
        ++size;
    }

    uint16_t pop(uint32_t& size) {
        auto hi = index_.find_first();
        auto lo = bitmap_[hi].find_first();
        bitmap_[hi].clear(lo);
        if (bitmap_[hi].empty())
            index_.clear(hi);
        --size;
        return (hi << 6) | lo;
    }
};

template< typename T, std::size_t Size > struct arena_free_list4 {
    static_assert(Size <= 64 * 256);
    
    void push(uint16_t value, uint32_t& size) {
        assert(value < Size);
        if (stack_size_ < stack_.size()) {
            stack_[stack_size_++] = value;
        } else {
            push_bitmap(value);
        }
        ++size;
    }

    uint16_t pop(uint32_t& size) {
        if (!stack_size_) {
            assert(size);
            assert(bitmap_size_);
            auto values = pop_bitmap();
        #if 1
            https://lemire.me/blog/2018/02/21/iterating-over-set-bits-quickly/
            uint64_t bits = values.second;
            while (bits != 0) {
                uint64_t t = bits & -bits;
                auto value = values.first | __builtin_ctzl(bits);
                assert(value < Size);
                stack_[stack_size_++] = value;
                bits ^= t;
            }
        #else
            for(size_t i = 0; i < 64; ++i) {
                if (values.second & (1ull << i)) {
                    auto value = values.first | i;
                    assert(value < Size);
                    stack_[stack_size_++] = value;
                }
            }
        #endif
        }

        --size;
        auto value = stack_[--stack_size_];
        assert(value < Size);
        return value;
    }

    void push_bitmap(uint16_t value) {
        assert(value < Size);
        index_.set(value >> 6);
        bitmap_[value >> 6].set(value & 63);
        assert(bitmap_size_ < Size);
        ++bitmap_size_;
    }

    std::pair<uint64_t, uint64_t> pop_bitmap() {
        auto hi = index_.find_first();
        uint64_t lo = bitmap_[hi];
        bitmap_[hi].clear();
        index_.clear(hi);
        assert(bitmap_size_ >= _mm_popcnt_u64(lo));
        bitmap_size_ -= _mm_popcnt_u64(lo);
        return {hi << 6, lo};
    }

    // metadata
    detail::bitset< 256 > index_;
    uint32_t stack_size_ = 0;
    uint32_t bitmap_size_ = 0;
    
    // detail::atomic_bitset< 256 > atomic_index_;
    //std::atomic<uint32_t> atomic_bitmap_size_ = 0;

    // and two pages
    std::array< uint16_t, 2048 > stack_; // 1st page
    std::array< detail::bitset< 64 >, 256 > bitmap_; // 2nd page (with atomic portion)
    //std::array< detail::bitset< 64 >, 256 > atomic_bitmap_;
};

template< std::size_t ArenaSize, std::size_t Size, std::size_t Alignment > class arena
    : public arena_metadata
{
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

    // TODO: move all metadata elsewhere
    static constexpr std::size_t Count = (ArenaSize - sizeof(arena_metadata))/(Size + 2);
    //static constexpr std::size_t Count = round_up((ArenaSize - sizeof(arena_metadata))/(Size + 2))/2;
    
    //arena_free_list< uint16_t, round_up(Count) > free_list_;
    //arena_free_list2< uint16_t, Count > free_list_;
    //arena_free_list3< uint16_t, round_up(Count) > free_list_;
    arena_free_list4< uint16_t, Count > free_list_;

public:
    arena() {
    #if defined(THREADS)
        tid_ = thread_id();
    #endif
        begin_ = (uint8_t*)this + sizeof(*this);
        size_class_ = Size;
        free_list_size_ = 0;
        for(std::size_t i = Count - 1; i > 0; --i)
            free_list_.push(i, free_list_size_);
    }

    uint8_t* begin() const { return begin_; }
    uint8_t* end() const { return (uint8_t*)this + ArenaSize; }
    
    void* allocate() {
        assert(size_class_ == Size);
        uint16_t index = free_list_.pop(free_list_size_);
        assert(index < Count);
        uint8_t* ptr = begin_ + index * Size;
        assert(is_ptr_valid(ptr));
        return ptr;
    }
    
    void deallocate(void* ptr) {
    #if defined(THREADS)
        if (tid_ != thread_id())
            std::abort();
    #endif
        assert(size_class_ == Size);
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / Size;
        free_list_.push(index, free_list_size_);
    }

    static constexpr std::size_t capacity() { return Count; }

    std::size_t size() const { return Count - free_list_size_; }

private:
    bool is_ptr_valid(void* ptr) {
        assert(is_ptr_in_range(ptr, Size, begin(), end()));
        assert(is_ptr_aligned(ptr, Alignment));
        return true;
    }
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

    bool empty() const {
        return values_.empty();
    }

    T pop() {
        std::pop_heap(values_.begin(), values_.end(), Compare{});
        T value = values_.back();
        values_.pop_back();
        return value;
    }

    const T& top() {
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

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t MmapSize = MaxSize + PageSize - 1;
    static constexpr std::size_t PageCount = MaxSize / PageSize;
    static_assert(PageCount <= std::numeric_limits< uint32_t >::max());

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
        uint32_t page = 0;
        if (deallocated_pages_.pop(page)) {
            ptr = get_page(page);
        } else {
            if (memory_size_ == PageCount) {
                fprintf(stderr, "Out of memory\n");
                std::abort();
            }
            ptr = (uint8_t*)memory_ + memory_size_.fetch_add(1, std::memory_order_relaxed) * PageSize;
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

    uint32_t get_page_index(void* ptr) const {
        assert(is_page_valid(ptr));
        return ((uint8_t*)ptr - (uint8_t*)memory_) / PageSize;
    }

private:
    bool is_page_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, PageSize, begin(), end()));
        assert(is_ptr_aligned(ptr, PageSize));
        return true;
    }

    void* mmap_ = 0;
    void* memory_ = 0;
    alignas(64) std::atomic<uint64_t> memory_size_ = 0;
    alignas(64) detail::atomic_bitset_heap< uint32_t, PageCount > deallocated_pages_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > struct arena_manager {
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);

    static constexpr std::size_t ArenaCount = MaxSize/ArenaSize;
    static constexpr std::size_t PageCount = MaxSize/PageSize;
    static constexpr std::size_t PageArenaCount = PageSize/ArenaSize;

    static constexpr std::size_t PageBitmapFull = (1 << PageArenaCount) - 1;

    static_assert(ArenaCount <= std::numeric_limits<uint32_t>::max());

    enum PageState {
        Deallocated = 0,
        Allocated = 1,
        Full = 2,
    };

    struct page_metadata {
        uint8_t state;
        detail::bitset<PageSize/ArenaSize> bitmap;
    };

    void* get_allocated_page() {
        while(!allocated_pages_.empty()) {
            void* page = page_manager_.get_page(allocated_pages_.top());
            auto& metadata = get_page_metadata(page);
            if (metadata.state == PageState::Deallocated) {
                allocated_pages_.pop();
                continue;
            }
            return page;
        }

        void* page = page_manager_.allocate_page();
        auto& metadata = get_page_metadata(page);
        metadata.bitmap.clear();
        metadata.state = PageState::Allocated;
        allocated_pages_.push(page_manager_.get_page_index(page));
        return page;
    }

    void* allocate_arena() {
        void* page = get_allocated_page();
        auto& metadata = get_page_metadata(page);
        assert(!metadata.bitmap.full());
        assert(metadata.state == PageState::Allocated);
        
        void* arena = 0;
        for (size_t i = 0; i < PageArenaCount; ++i) {
            if (!metadata.bitmap.get(i)) {
                metadata.bitmap.set(i);
                arena = (uint8_t*)page + ArenaSize * i;
                if (metadata.bitmap.full()) {
                    assert(page_manager_.get_page(allocated_pages_.top()) == page);
                    allocated_pages_.pop();
                }
                
                break;
            }
        }

        assert(is_arena_valid(arena));

    #if defined(STATS)
        ++stats.arenas_allocated;
    #endif
        return arena;
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
        auto& metadata = get_page_metadata(page);
        assert(metadata.state == PageState::Allocated);

        if (metadata.bitmap.full())
            allocated_pages_.push(page_manager_.get_page_index(page));

        int index = ((uint8_t*)ptr - (uint8_t*)page)/ArenaSize;
        assert(index < PageArenaCount);
        metadata.bitmap.clear(index);
        if (metadata.bitmap.empty()) {        
            metadata.state = PageState::Deallocated;
            page_manager_.deallocate_page(page);
        }

    #if defined(STATS)
        --stats.arenas_allocated;
    #endif
    }

    template< std::size_t SizeClass > bool get_arena_state(void* ptr) {
        assert(is_arena_valid(ptr));
        void* page = page_manager_.get_page(ptr);
        auto& metadata = get_page_metadata(page);
        if (metadata.state == PageState::Allocated) {
            auto& ametadata = *(arena_metadata*)ptr;
            int index = ((uint8_t*)ptr - (uint8_t*)page)/ArenaSize;
            return metadata.bitmap.get(index) && ametadata.size_class_ == SizeClass;
        }
        return false;
    }

private:
    page_metadata& get_page_metadata(void* page) {
        return metadata_[page_manager_.get_page_index(page)];
    }

    bool is_arena_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, ArenaSize, page_manager_.begin(), page_manager_.end()));
        assert(is_ptr_aligned(ptr, ArenaSize));
        return true;
    }

    page_manager< PageSize, MaxSize > page_manager_;
    detail::bitset_heap< uint32_t, PageCount > allocated_pages_;
    page_metadata metadata_[PageCount];
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > class arena_allocator_base {
    static_assert((PageSize & (PageSize - 1)) == 0);
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static_assert((MaxSize & (MaxSize - 1)) == 0);

    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);
    
protected:
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
        auto arena = get_cached_arena<SizeClass>();
        if (arena->size() < arena->capacity())
            return arena->allocate();

        assert(arena->size() == arena->capacity());
        pop_arena<SizeClass>();
        reset_cached_arena<SizeClass>();
        goto again;
    }

    template< size_t SizeClass > void deallocate_impl(void* ptr) noexcept {
        auto arena = get_arena<SizeClass>(ptr);
        arena->deallocate(ptr);
        if(arena->size() == 0) {
            deallocate_arena<SizeClass>(arena);
        } else if(arena->size() == arena->capacity() - 1) {
            push_arena<SizeClass>(arena);
            reset_cached_arena<SizeClass>();
        }
    }

    template< size_t SizeClass > arena<ArenaSize, SizeClass, 8>* get_cached_arena() {
        auto offset = size_class_offset(SizeClass);
        assert(classes_cache_[offset] == arena_manager_.get_arena(classes_[offset].top()));
        return (arena<ArenaSize, SizeClass, 8>*)classes_cache_[offset];        
    }

    template< size_t SizeClass > void* reset_cached_arena() {
        auto offset = size_class_offset(SizeClass);
    again:
        while(!classes_[offset].empty()) {
            auto* buffer = (arena<ArenaSize, SizeClass, 8>*)arena_manager_.get_arena(classes_[offset].top());
            if (arena_manager_.template get_arena_state< SizeClass >(buffer) && buffer->size() != buffer->capacity()) {
                classes_cache_[offset] = buffer;
                return buffer;
            } else {
                pop_arena<SizeClass>();
                if(classes_[offset].size())
                    goto again;
            }
        }

        void* buffer = allocate_arena<SizeClass>();
        classes_cache_[offset] = buffer;
        return buffer;
    }

    template< size_t SizeClass > arena<ArenaSize, SizeClass, 8>* get_arena(void* ptr) {
        return (arena<ArenaSize, SizeClass, 8>*)arena_manager_.get_arena(ptr);
    }

    template< size_t SizeClass > arena<ArenaSize, SizeClass, 8>* allocate_arena() {
        auto offset = size_class_offset(SizeClass);
        void* ptr = arena_manager_.allocate_arena();
        auto* buffer = new (ptr) arena<ArenaSize, SizeClass, 8>;
        classes_[offset].push(arena_manager_.get_arena_index(buffer));
        return buffer;
    }

    template< size_t SizeClass > void deallocate_arena(void* ptr) {
        auto offset = size_class_offset(SizeClass);
        if (classes_[offset].top() != arena_manager_.get_arena_index(ptr)) {
            arena_manager_.deallocate_arena(ptr);
        }
    }

    template< size_t SizeClass > void pop_arena() {
        auto offset = size_class_offset(SizeClass);
        classes_[offset].pop();
    }
    
    template< size_t SizeClass > void push_arena(void* ptr) {
        (void)ptr;
        auto offset = size_class_offset(SizeClass);
        classes_[offset].push(arena_manager_.get_arena_index(ptr));
    }

    static arena_manager<PageSize, ArenaSize, MaxSize> arena_manager_;

    static std::array<elastic_heap<uint32_t, MaxSize/ArenaSize>, 23> classes_;
    static std::array<void*, 23> classes_cache_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> arena_manager<PageSize, ArenaSize, MaxSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::arena_manager_;
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<elastic_heap<uint32_t, MaxSize/ArenaSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::classes_;
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<void*, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::classes_cache_;

template <typename T > class allocator
    : public arena_allocator_base< 1<<21, 1<<17, 1ull<<40 > 
{
    template <typename U> friend class allocator;
    
public:
    using value_type    = T;

    allocator() noexcept {
        reset_cached_arena<size_class<T>()>();
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
bool operator == (const allocator<T>& lhs, const allocator<U>& rhs) noexcept {
    return true;
}

template <typename T, typename U>
bool operator != (const allocator<T>& x, const allocator<U>& y) noexcept {
    return !(x == y);
}

}