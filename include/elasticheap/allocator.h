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
            fprintf(stderr, "%s: %d: Assertion failed: " #exp "\n", __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0);
#endif

#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __likely(cond) __builtin_expect((cond), true)
#define __unlikely(cond) __builtin_expect((cond), false)
#define __failure(msg) do { fprintf(stderr, "%s:%d: %s: %s\n", \
    __FILE__, __LINE__, __PRETTY_FUNCTION__, msg); } while(0)

//#define STATS
#define THREADS
#define MAGIC
#define ELASTIC_BITSET_HEAP

namespace elasticheap {
    static constexpr std::size_t MetadataPageSize = 4096;

#if defined(STATS)
    struct allocator_stats {
        uint64_t pages_allocated;
    };

    static allocator_stats stats;

    static void print_stats() {
        fprintf(stderr, "pages_allocated: %lu\n", stats.pages_allocated);
    }
#endif

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

//
// Freelists:
//  8  bytes -  8 pages
//  16 bytes -  4 pages
//  32 bytes -  2 pages
//  64 bytes -  1 page
//
// Separate segment managers for arena metadata
//

template< typename T, std::size_t Size > struct arena_free_list {
    static_assert(Size <= std::numeric_limits<T>::max());

    detail::bitset<Size> bitmap_;
    uint32_t index_ = 0;

    void push(T value, uint32_t& size) {
        assert(value < Size);
        assert(size < Size);
        bitmap_.set(value);
        ++size;
    }

    T pop(uint32_t& size) {
        assert(size);
        --size;
        return bitmap_.pop_first(index_);
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
        if (__likely(stack_size_ < stack_.size())) {
            stack_[stack_size_++] = value;
        } else {
            push_bitmap(value);
        }
        ++size;
    }

    uint16_t pop(uint32_t& size) {
        if (__unlikely(!stack_size_)) {
            assert(size);
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
    }

    std::pair<uint64_t, uint64_t> pop_bitmap() {
        auto hi = index_.find_first();
        uint64_t lo = bitmap_[hi];
        bitmap_[hi].clear();
        index_.clear(hi);
        return {hi << 6, lo};
    }

    // metadata
    detail::bitset< 256 > index_;
    uint32_t stack_size_ = 0;

    // detail::atomic_bitset< 256 > atomic_index_;
    //std::atomic<uint32_t> atomic_bitmap_size_ = 0;

    // and two pages
    std::array< uint16_t, 2048 > stack_; // 1st page
    std::array< detail::bitset< 64 >, 256 > bitmap_; // 2nd page (with atomic portion)
    //std::array< detail::bitset< 64 >, 256 > atomic_bitmap_;
};

template< typename T, std::size_t Size > struct arena_free_list5 {
    static_assert(Size <= 64 * 256);

    void push(uint16_t value, uint32_t& size) {
        assert(value < Size);
        ++size;
        if (__likely(stack_size_ < stack_.size())) {
            stack_[stack_size_++] = value;
        } else {
            bitmap_.set(value);
        }
    }

    uint16_t pop(uint32_t& size) {
        assert(size > 0);
        --size;
        if (__unlikely(!stack_size_)) {
            return bitmap_.pop_first(bitmap_index_);
        }

        auto value = stack_[--stack_size_];
        assert(value < Size);
        return value;
    }

    // metadata
    uint32_t stack_size_ = 0;
    uint32_t bitmap_index_ = 0;
    uint32_t bitmap_size_ = 0;
    std::atomic<std::size_t > atomic_bitmap_index_;
    std::atomic<uint32_t> atomic_bitmap_size_;

    // and two pages
    std::array< uint16_t, 1024 + 512 > stack_;    // 1st page
    detail::bitset< 64 * 256 > bitmap_;     // 2nd page (with atomic portion)
    detail::atomic_bitset< 64 * 256 > atomic_bitmap_;
};

struct arena_descriptor_base {
#if defined(MAGIC)
    uint32_t magic_ = 0xDEADBEEF;
#endif
#if defined(THREADS)
    uint64_t tid_;
#endif
    uint8_t* begin_;
    uint32_t size_class_;
    uint32_t free_list_size_;
};

static constexpr std::size_t DescriptorSize = 1<<16;

template< std::size_t ArenaSize, std::size_t SizeClass, std::size_t Alignment = 8 > struct arena_descriptor: arena_descriptor_base {
    static constexpr std::size_t Capacity = ArenaSize/SizeClass;

    arena_descriptor(void* buffer) {
    #if defined(THREADS)
        tid_ = thread_id();
    #endif
        begin_ = (uint8_t*)buffer;
        size_class_ = SizeClass;
        free_list_size_ = 0;
        for(int i = Capacity - 1; i >= 0; --i) {
            free_list_.push(i, free_list_size_);
        }
    }

    //using free_list_type = arena_free_list< uint16_t, Capacity >;
    using free_list_type = arena_free_list2< uint16_t, Capacity >;
    //using free_list_type = arena_free_list3< uint16_t, Capacity >;
    //using free_list_type = arena_free_list4< uint16_t, Capacity >;
    //using free_list_type = arena_free_list5< uint16_t, Capacity >;

    void* allocate() {
    #if defined(MAGIC)
        assert(magic_ == 0xDEADBEEF);
    #endif
        assert(size_class_ == SizeClass);
        uint16_t index = free_list_.pop(free_list_size_);
        assert(index < Capacity);
        uint8_t* ptr = begin_ + index * SizeClass;
        assert(is_ptr_valid(ptr));
        return ptr;
    }

    void deallocate(void* ptr) {
    #if defined(MAGIC)
        assert(magic_ == 0xDEADBEEF);
    #endif
    #if defined(THREADS)
        assert(tid_ == thread_id());
    #endif
        assert(size_class_ == SizeClass);
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / SizeClass;
        free_list_.push(index, free_list_size_);
    }

    static constexpr std::size_t capacity() { return Capacity; }

    std::size_t size() { return Capacity - free_list_size_; }

    uint8_t* begin() { return begin_; }
    uint8_t* end() { return begin_ + ArenaSize; }

private:
    bool is_ptr_valid(void* ptr) {
        assert(is_ptr_in_range(ptr, SizeClass, begin(), end()));
        assert(is_ptr_aligned(ptr, Alignment));
        return true;
    }

    free_list_type free_list_;
    static_assert(sizeof(free_list_type) + sizeof(arena_descriptor_base) <= DescriptorSize);
};

template< typename T, std::size_t Size, std::size_t PageSize = 4096 > struct elastic_vector {
    using size_type = uint32_t;
    static_assert(sizeof(T) * Size < std::numeric_limits<size_type>::max());

    elastic_vector() {
        memory_ = (T*)mmap(0, sizeof(T) * Size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory_ == MAP_FAILED)
            __failure("mmap");
    }

    ~elastic_vector() {
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
            if (mprotect((uint8_t*)memory_ + size_commited_ * sizeof(T), PageSize, PROT_READ | PROT_WRITE) == -1) {
                // TODO
                // __failure("mprotect");
            }
            size_commited_ += PageSize/sizeof(T);
        }
    }

    void shrink() {
        if (size_ + PageSize/sizeof(T) < size_commited_) {
            if (mprotect((uint8_t*)memory_ + size_commited_ * sizeof(T) - PageSize/sizeof(T), PageSize, PROT_NONE) == -1) {
                // TODO
                // __failure("mprotect");
            }
            size_commited_ -= PageSize/sizeof(T);
        }
    }

    T* memory_ = 0;
    size_type size_ = 0;
    size_type size_commited_ = 0;
};

template< typename T, std::size_t Size, typename Compare = std::greater<> > struct elastic_heap {
    void push(T value) {
        //assert(std::find(values_.begin(), values_.end(), value) == values_.end());
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
    elastic_vector<T, Size> values_;
};

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

template< typename T, std::size_t Size, std::size_t PageSize > struct descriptor_manager {
    static_assert(PageSize > sizeof(T));
    static_assert(PageSize % sizeof(T) == 0);
    static constexpr std::size_t MmapSize = (sizeof(T) * Size + PageSize - 1) & ~(PageSize - 1);

    descriptor_manager()
        : mmap_(mmap(0, MmapSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
        , values_(align<PageSize>(mmap_))
    {
        if (mmap_ == MAP_FAILED)
            __failure("mmap");
    }

    ~descriptor_manager() {
        munmap(mmap_, MmapSize);
    }

    T* allocate_descriptor(std::size_t i) {
        return values_.acquire(i);
    }

    void deallocate_descriptor(void* ptr) {
        return values_.release((T*)ptr);
    }

    void deallocate_descriptor(std::size_t i) {
        return values_.release(i);
    }

    uint32_t get_descriptor_index(void* desc) {
        return values_.get_index((T*)desc);
    }

    T* get_descriptor(uint32_t index) {
        return values_.get(index);
    }

private:
    void* mmap_ = 0;
    elastic_array< T, Size, PageSize > values_;
};

template< typename T, std::size_t Capacity, std::size_t PageSize > struct elastic_atomic_bitset_heap {
    static constexpr std::size_t MmapSize = sizeof(detail::atomic_bitset<Capacity>) + PageSize - 1;
    static_assert(Capacity <= std::numeric_limits<uint32_t>::max());

    using counter_type =
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint8_t>::max(), uint8_t,
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint16_t>::max(), uint16_t,
        std::conditional_t< PageSize / sizeof(T) <= std::numeric_limits<uint32_t>::max(), uint32_t,
        uint64_t
    >>>;

    static constexpr std::size_t capacity() { return Capacity; }

    elastic_atomic_bitset_heap()
        : mmap_((uint8_t*)mmap(0, MmapSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
    {
        if (mmap_ == MAP_FAILED)
            __failure("mmap");
        bitmap_ = (detail::atomic_bitset<Capacity>*)align<PageSize>(mmap_);
        range_.store(Capacity, std::memory_order_relaxed);
    }

    void push(T value) {
        assert(value < Capacity);
        if (page_refs_[page(value)]++ == 0) {
            if (mprotect((uint8_t*)bitmap_ + page(value) * PageSize, PageSize, PROT_READ|PROT_WRITE) == -1)
                __failure("mprotect");
        }

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

    bool empty(const std::atomic<uint64_t>& range) const {
        return (uint32_t)range.load(std::memory_order_relaxed) == Capacity;
    }

    bool erase(T value) {
        if (!bitmap_->get(value))
            return false;

        bool cleared = bitmap_->clear(value);

        if (--page_refs_[page(value)] == 0) {
            if (madvise((uint8_t*)bitmap_ + page(value) * PageSize, PageSize, MADV_DONTNEED) == -1)
                __failure("madvise");
        }

        // TODO: recalculate the range
        // Note that there is an use-case of erase/push that does not modify the range
        std::atomic_thread_fence(std::memory_order_release);
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

    bool top(const std::atomic<uint64_t>& range, T& value) const {
        auto r = range.load(std::memory_order_acquire);
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
                        assert(page_refs_[page(i)] > 0);
                        if (--page_refs_[page(i)] == 0) {
                            if (madvise((uint8_t*)bitmap_ + page(i) * PageSize, PageSize, MADV_DONTNEED) == -1)
                                __failure("madvise");
                        }

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

                    if (--page_refs_[page(i)] == 0) {
                        if (madvise((uint8_t*)bitmap_ + page(i) * PageSize, PageSize, MADV_DONTNEED) == -1)
                            __failure("madvise");
                    }

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
        if (page_refs_[page(value)] > 0)
            return bitmap_->get(value);
        return false;
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

    std::array<counter_type, (Capacity + PageSize * 8 - 1) / (PageSize * 8) > page_refs_;

    uint8_t* mmap_;
    detail::atomic_bitset<Capacity>* bitmap_;
    std::atomic<uint64_t> range_;
};

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t MmapSize = MaxSize + PageSize - 1;
    static constexpr std::size_t PageCount = MaxSize / PageSize;
    static_assert(PageCount <= std::numeric_limits< uint32_t >::max());

    page_manager()
        : deallocated_pages_()
    {
        mmap_ = (uint8_t*)mmap(0, MmapSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mmap_ == MAP_FAILED)
            __failure("mmap");

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
            assert(!is_page_deallocated(ptr));
        } else {
            if (memory_size_ == PageCount) {
                __failure("out of memory");
            }
            ptr = (uint8_t*)memory_ + memory_size_.fetch_add(1, std::memory_order_relaxed) * PageSize;
        }

        assert(is_page_valid(ptr));
        mprotect(ptr, PageSize, PROT_READ | PROT_WRITE);
    #if defined(STATS)
        ++stats.pages_allocated;
    #endif
        return ptr;
    }

    void deallocate_page(void* ptr) {
        assert(is_page_valid(ptr));
        assert(!is_page_deallocated(ptr));

        //mprotect(ptr, PageSize, PROT_NONE);
        madvise(ptr, PageSize, MADV_DONTNEED);

        deallocated_pages_.push(get_page_index(ptr));
    #if defined(STATS)
        --stats.pages_allocated;
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

    bool is_page_deallocated(void* page) {
        return deallocated_pages_.get(get_page_index(page));
    }

private:
    bool is_page_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, PageSize, begin(), end()));
        assert(is_ptr_aligned(ptr, PageSize));
        return true;
    }

    alignas(64) std::atomic<uint64_t> memory_size_ = 0;
    void* mmap_ = 0;
    void* memory_ = 0;
    elastic_atomic_bitset_heap< uint32_t, PageCount, MetadataPageSize > deallocated_pages_;
};

template< typename T, std::size_t Capacity, std::size_t PageSize > struct elastic_bitset_heap {
    static constexpr std::size_t MmapSize = sizeof(detail::bitset<Capacity>) + PageSize - 1;

    elastic_bitset_heap()
        : mmap_(mmap(0, MmapSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))
    {
        if (mmap_ == MAP_FAILED)
            __failure("mmap");
        bitmap_ = (detail::bitset<Capacity>*)align<PageSize>(mmap_);
    }

    ~elastic_bitset_heap() {
        munmap(mmap_, MmapSize);
    }

    void push(T value) {
        assert(value < Capacity);
        if (page_refs_[page(value)]++ == 0) {
            if (mprotect((uint8_t*)bitmap_ + page(value) * PageSize, PageSize, PROT_READ|PROT_WRITE) == -1)
                __failure("mprotect");
        }

        assert(!bitmap_->get(value));
        bitmap_->set(value);
        ++size_;
        min_ = min_ > value ? value : min_;
        max_ = max_ < value ? value : max_;
    }

    bool empty() const {
        return size_ == 0;
    }

    T pop() {
        assert(!empty());
        T min = min_;
        assert(page_refs_[page(min)] > 0);
        assert(bitmap_->get(min));
        bitmap_->clear(min);
        if (--page_refs_[page(min)] == 0) {
            if (madvise((uint8_t*)bitmap_ + page(min) * PageSize, PageSize, MADV_DONTNEED) == -1)
                __failure("madvise");
        }

        --size_;
        for(std::size_t i = min + 1; i <= max_; ++i) {
            if (bitmap_->get(i)) {
                min_ = i;
                return min;
            }
        }

        min_ = Capacity;
        max_ = 0;

        return min;
    }

    const T& top() {
        assert(!empty());
        return min_;
    }

    std::size_t size() const { return size_; }
    static constexpr std::size_t capacity() { return Capacity; }

    static std::size_t page(std::size_t index) {
        assert(index < Capacity);
        return index / (PageSize * 8);
    }

    bool get(std::size_t index) const {
        if (page_refs_[page(index)] > 0) {
            return bitmap_->get(index);
        }
        return false;
    }

private:
    std::array< uint32_t, (Capacity + PageSize * 8 - 1) / (PageSize * 8) > page_refs_ = {0};

    std::size_t size_ = 0;
    T min_ = Capacity;
    T max_ = 0;

    void* mmap_;
    detail::bitset<Capacity>* bitmap_;
};

template< std::size_t PageSize, std::size_t SegmentSize, std::size_t MaxSize > struct segment_manager {
    static_assert((SegmentSize & (SegmentSize - 1)) == 0);

    static constexpr std::size_t SegmentCount = MaxSize/SegmentSize;
    static constexpr std::size_t PageCount = MaxSize/PageSize;
    static constexpr std::size_t PageSegmentCount = PageSize/SegmentSize;

    static_assert(SegmentCount <= std::numeric_limits<uint32_t>::max());

    struct page_descriptor {
        detail::atomic_bitset<PageSize/SegmentSize> bitmap;
    };

    void* get_allocated_page() {
        uint32_t top;
        while(allocated_pages_.pop(top)) {
            void* page = page_manager_.get_page(top);
            if (page_manager_.is_page_deallocated(page)) {
                continue;
            }

            return page;
        }

        void* page = page_manager_.allocate_page();
        auto index = page_manager_.get_page_index(page);
        auto* pdesc = page_descriptors_.allocate_descriptor(index);
        pdesc->bitmap.clear();
        return page;
    }

    void* allocate_segment() {
        void* page = get_allocated_page();
        auto* pdesc = page_descriptors_.get_descriptor(page_manager_.get_page_index(page));
        assert(!pdesc->bitmap.full());

        void* segment = 0;
        for (size_t i = 0; i < PageSegmentCount; ++i) {
            if (!pdesc->bitmap.get(i)) {
                auto word = pdesc->bitmap.set(i);
                segment = (uint8_t*)page + SegmentSize * i;
                if (pdesc->bitmap.popcount(word) + 1 != pdesc->bitmap.size())
                    allocated_pages_.push(page_manager_.get_page_index(page));
                break;
            }
        }

        assert(is_segment_valid(segment));
        return segment;
    }

    void* get_segment(void* ptr) const {
        assert(is_ptr_in_range(ptr, 1, page_manager_.begin(), page_manager_.end()));
        return mask<SegmentSize>(ptr);
    }

    void* get_segment(uint32_t index) const {
        void* ptr = (uint8_t*)page_manager_.begin() + SegmentSize * index;
        assert(is_segment_valid(ptr));
        return ptr;
    }

    uint32_t get_segment_index(void* ptr) const {
        assert(is_segment_valid(get_segment(ptr)));
        return ((uint8_t*)ptr - (uint8_t*)page_manager_.begin()) / SegmentSize;
    }

    void deallocate_segment(void* ptr) {
        assert(is_segment_valid(ptr));
        void* page = page_manager_.get_page(ptr);
        auto* pdesc = page_descriptors_.get_descriptor(page_manager_.get_page_index(page));

        bool erased = allocated_pages_.erase(page_manager_.get_page_index(page));

        int index = ((uint8_t*)ptr - (uint8_t*)page)/SegmentSize;
        assert(index < PageSegmentCount);
        auto word = pdesc->bitmap.clear(index);
        if (erased) {
            if (pdesc->bitmap.popcount(word) == 1) {
                page_manager_.deallocate_page(page);
            } else {
                allocated_pages_.push(page_manager_.get_page_index(page));
            }
        } else {
            // TODO: revisit full_value and popcounts for larger bitmaps
            static_assert(pdesc->bitmap.size() <= 64);
            if (pdesc->bitmap.popcount(word) == pdesc->bitmap.size())
                allocated_pages_.push(page_manager_.get_page_index(page));
        }
    }

    void* get_page(void* ptr) {
        return page_manager_.get_page(ptr);
    }

    page_descriptor& get_page_descriptor(void* page) {
        return *page_descriptors_.get_descriptor(page_manager_.get_page_index(page));
    }

    bool is_page_deallocated(void* page) {
        return page_manager_.is_page_deallocated(page);
    }

    bool is_segment_valid(void* ptr) const {
        assert(is_ptr_in_range(ptr, SegmentSize, page_manager_.begin(), page_manager_.end()));
        assert(is_ptr_aligned(ptr, SegmentSize));
        return true;
    }

private:
    // Global
    page_manager< PageSize, MaxSize > page_manager_;
    descriptor_manager< page_descriptor, PageCount, MetadataPageSize > page_descriptors_;

    // Local, but could also be Global.
    elastic_atomic_bitset_heap< uint32_t, PageCount, MetadataPageSize > allocated_pages_;
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
            __failure("invalid class size");
        }
    }

    template< size_t SizeClass > void* allocate_impl() {
    again:
        auto* desc = get_cached_descriptor<SizeClass>();
        /*
        if (__likely(!desc->empty_local()) {
            return desc->allocate_local();
        }

        if (__likely(!desc->empty_global()) {
            return desc->allocate_global();
        }
        */

        if (__likely(desc->size() < desc->capacity()))
            return desc->allocate();

        assert(desc->size() == desc->capacity());
        pop_descriptor<SizeClass>();
        reset_cached_descriptor<SizeClass>();
        goto again;
    }

    template< size_t SizeClass > void deallocate_impl(void* ptr) noexcept {
        auto* desc = get_descriptor<SizeClass>(ptr);
        /*
        if (__likely(desc->thread_id() == thread_id())) {
            desc->deallocate_local(ptr);
            if (__unlikely(desc->size_local() == 0)) {
            }
        } else {
            desc->deallocate_global(ptr);
        }
        */

        // TODO: cache heap is not MT-safe yet
        desc->deallocate(ptr);
        if(__unlikely(desc->size() == 0)) {
            deallocate_descriptor<SizeClass>(desc);
        } else if(__unlikely(desc->size() == desc->capacity() - 1)) {
            push_descriptor<SizeClass>(desc);
            reset_cached_descriptor<SizeClass>();
        }
    }

    template< size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* get_cached_descriptor() {
        auto size = size_class_offset(SizeClass);
        assert((void*)size_class_cache_[size] == (void*)descriptor_manager_.get_descriptor(size_classes_[size].top()));
        return (arena_descriptor< ArenaSize, SizeClass >*)size_class_cache_[size];
    }

    template< size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* reset_cached_descriptor() {
        auto size = size_class_offset(SizeClass);
    again:
        while(!size_classes_[size].empty()) {
            auto* desc = (arena_descriptor<ArenaSize, SizeClass>*)descriptor_manager_.get_descriptor(size_classes_[size].top());
            if ((validate_descriptor_state< SizeClass >(desc) && desc->size() != desc->capacity())) {
                size_class_cache_[size] = desc;
                return desc;
            } else {
                pop_descriptor<SizeClass>();
                if(size_classes_[size].size())
                    goto again;
            }
        }

        auto* desc = allocate_descriptor<SizeClass>();
        assert(desc->size() == 0);
        size_class_cache_[size] = desc;
        return desc;
    }

    template< typename std::size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* get_descriptor(void* ptr) {
        auto index = segment_manager_.get_segment_index(ptr);
        return (arena_descriptor<ArenaSize, SizeClass>*)descriptor_manager_.get_descriptor(index);
    }

    template< size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* allocate_descriptor() {
        auto size = size_class_offset(SizeClass);
        void* buffer = segment_manager_.allocate_segment();
        auto* desc = (arena_descriptor<ArenaSize, SizeClass>*)descriptor_manager_.allocate_descriptor(segment_manager_.get_segment_index(buffer));
        new(desc) arena_descriptor< ArenaSize, SizeClass >(buffer);
        size_classes_[size].push(descriptor_manager_.get_descriptor_index(desc));
        return desc;
    }

    template< size_t SizeClass > void deallocate_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
        auto size = size_class_offset(SizeClass);
        if (size_classes_[size].top() != descriptor_manager_.get_descriptor_index(desc)) {
            segment_manager_.deallocate_segment(desc->begin());
            descriptor_manager_.deallocate_descriptor(desc);
        }
    }

    template< size_t SizeClass > void pop_descriptor() {
        auto size = size_class_offset(SizeClass);
        size_classes_[size].pop();
    }

    template< size_t SizeClass > void push_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
        (void)desc;
        auto size = size_class_offset(SizeClass);
        // TODO: a case where descriptor is in a heap, but we cross the size - 1
    #if defined(ELASTIC_BITSET_HEAP)
        if (!size_classes_[size].get(descriptor_manager_.get_descriptor_index(desc)))
    #endif
        size_classes_[size].push(descriptor_manager_.get_descriptor_index(desc));
    }

    template< std::size_t SizeClass > bool validate_descriptor_state(arena_descriptor<ArenaSize, SizeClass>* desc) {
        auto index = descriptor_manager_.get_descriptor_index(desc);
        auto* segment = segment_manager_.get_segment(index);
        void* page = segment_manager_.get_page(segment);
        if (!segment_manager_.is_page_deallocated(page)) {
            auto& pdesc = segment_manager_.get_page_descriptor(page);
            int pindex = ((uint8_t*)segment - (uint8_t*)page)/ArenaSize;

            return pdesc.bitmap.get(pindex) && desc->size_class_ == SizeClass && desc->tid_ == thread_id();
        }
        return false;
    }

    // TOOD: use some more separated global/local state
    // Global       - shared
    // Local        - thread-local or cpu-local
    // Thread-local - thread-local
    //

    static segment_manager<PageSize, ArenaSize, MaxSize> segment_manager_;
    // Global
    static descriptor_manager<std::array<uint8_t, DescriptorSize >, MaxSize / ArenaSize, PageSize> descriptor_manager_;
    // TODO: using bitmap has a little drawback that each descriptor can be from different page,
    // so the bitmap can end up very sparse. To mitigate that, each thread can get a reserved range
    // of pages, so its descriptors will fill bitmap pages. Unfortunately lock-free heap is a no-go.
    // With 2mb page/8 arenas, the reservation will be 1Gb to fill one page bitmap completely.
    //
    // Allocation/deallocation race
    //  1) allocation uses cached arena. This is safe, as cached object can't be deallocated.
    //  2) allocation - when cache needs to be updated, allocation pops next element
    //      and moves it to the cache
    //  3) during deallocation, cached arena stays intact
    //  4) deallocation - any other arena is removed from list and decommited
    // The arena does not have to be removed from any list, it just needs to be atomically marked.
    // as CACHED or DECOMMITED.

    // Using elastic_heap is simpler and also a bit faster (and needs more space), yet
    // it will never be lock-free.

    // Local
#if !defined(ELASTIC_BITSET_HEAP)
    static std::array<elastic_heap<uint32_t, MaxSize/ArenaSize>, 23> size_classes_;
#else
    static std::array<elastic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> size_classes_;
#endif
    // Thread-local
    static std::array<arena_descriptor_base*, 23> size_class_cache_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> segment_manager<PageSize, ArenaSize, MaxSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::segment_manager_;

template < std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> descriptor_manager<std::array<uint8_t, DescriptorSize>, MaxSize / ArenaSize, PageSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::descriptor_manager_;

#if !defined(ELASTIC_BITSET_HEAP)
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<elastic_heap<uint32_t, MaxSize/ArenaSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_classes_;
#else
template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<elastic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_classes_;
#endif

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<arena_descriptor_base*, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_class_cache_;

template <typename T > class allocator
    : public arena_allocator_base< 1<<21, 1<<17, 1ull<<40 >
{
    template <typename U> friend class allocator;

public:
    using value_type    = T;

    allocator() noexcept {
        reset_cached_descriptor<size_class<T>()>();
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
