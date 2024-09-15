//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/bitset.h>
#include <elasticheap/detail/bitset_heap.h>
#include <elasticheap/detail/atomic_bitset_heap.h>
#include <elasticheap/detail/elastic_array.h>
#include <elasticheap/detail/elastic_atomic_array.h>
#include <elasticheap/detail/elastic_atomic_bitset_heap.h>
#include <elasticheap/detail/utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <sys/mman.h>

//#define STATS
#define THREADS
#define MAGIC

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

enum {
    DescriptorNone = 0,
    DescriptorCached = 1,
    DescriptorFull = 2,
    DescriptorQueued = 4,
    DescriptorEmpty = 8,
    DescriptorGlobal = 16,
    DescriptorDone = DescriptorFull | DescriptorQueued | DescriptorEmpty,
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

    std::atomic<uint64_t> state_;
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

    std::size_t deallocate(void* ptr) {
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
        return size(); // TODO: for MT, this needs to be atomic
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

// TODO: this is somehow useless class
template< typename T, std::size_t Size, std::size_t PageSize > struct descriptor_manager {
    static_assert(PageSize > sizeof(T));
    static_assert(PageSize % sizeof(T) == 0);
    static constexpr std::size_t MmapSize = (sizeof(T) * Size + PageSize - 1) & ~(PageSize - 1);

    descriptor_manager()
        : mmap_(detail::memory::reserve(MmapSize))
        , values_(align<PageSize>(mmap_))
    {
    }

    ~descriptor_manager() {
        detail::memory::free(mmap_, MmapSize);
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
    detail::elastic_atomic_array< T, Size, PageSize > values_;
};

template< std::size_t PageSize, std::size_t MaxSize > struct page_manager {
    static constexpr std::size_t MmapSize = MaxSize + PageSize - 1;
    static constexpr std::size_t PageCount = MaxSize / PageSize;
    static_assert(PageCount <= std::numeric_limits< uint32_t >::max());

    page_manager()
        : deallocated_pages_()
    {
        mmap_ = (uint8_t*)detail::memory::reserve(MmapSize);
        memory_ = align<PageSize>(mmap_);
    }

    ~page_manager() {
        detail::memory::free(mmap_, MmapSize);
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
        detail::memory::commit(ptr, PageSize);
    #if defined(STATS)
        ++stats.pages_allocated;
    #endif
        return ptr;
    }

    void deallocate_page(void* ptr) {
        assert(is_page_valid(ptr));
        assert(!is_page_deallocated(ptr));

        detail::memory::decommit(ptr, PageSize);

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
    detail::elastic_atomic_bitset_heap< uint32_t, PageCount, MetadataPageSize > deallocated_pages_;
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
    detail::elastic_atomic_bitset_heap< uint32_t, PageCount, MetadataPageSize > allocated_pages_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > class arena_allocator_base {
    static_assert((PageSize & (PageSize - 1)) == 0);
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static_assert((MaxSize & (MaxSize - 1)) == 0);

    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);

    static std::atomic<uint64_t> counter_;

    template< typename T > void init_descriptor_state(T* desc, uint64_t state) {
        desc->state_.store(pack_descriptor_state(state, counter_.fetch_add(1, std::memory_order_relaxed) & ~(0xFFull << 56)), std::memory_order_relaxed);
    }

    template< typename T > uint64_t read_descriptor_state(T* desc) {
        return desc->state_.load(std::memory_order_acquire) & 0xFF;
    }

    static uint64_t pack_descriptor_state(uint64_t state, uint64_t counter) {
        assert(state <= 0xFF);
        assert(counter < (0xFFull << 56));
        return state | (counter << 8);
    }

    static std::pair< uint64_t, uint64_t > unpack_descriptor_state(uint64_t state) {
        return { state & 0xFF, state >> 8 };
    }

    template < typename T > uint64_t update_descriptor_state(T* desc, uint64_t state) {
        auto initial = desc->state_.load(std::memory_order_relaxed);
        auto current = initial;
        while(true) {
            auto unpacked = unpack_descriptor_state(current);
            if (desc->state_.compare_exchange_strong(current, pack_descriptor_state(unpacked.first | state, unpacked.second)), std::memory_order_release) {
                //assert((current.state & ~state) == 0);
                return unpacked.first | state;
            } else if (unpacked.second != unpack_descriptor_state(initial).second) {
                return 0;
            }
        }
    }

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

        //
        // Descriptor state tracking:
        //  Single thread allocates
        //  Multiple threads deallocate
        //
        //  allocate():
        //      if not full
        //          return alloc()
        //
        //  deallocate():
        //      if TLS
        //          if fully locally empty
        //              destroy
        //
        //      size = dealloc()
        //      if size = N - 1
        //          push()
        //          if size() == 0
        //              erase() && destroy
        //
        //      if size == 0
        //          erase() && destroy
        //
        //

        if (update_descriptor_state(desc, DescriptorFull) == DescriptorDone)
            deallocate_descriptor<SizeClass>(desc);

        reset_cached_descriptor<SizeClass>();
        goto again;
    }

    template< size_t SizeClass > void deallocate_impl(void* ptr) noexcept {
        auto* desc = get_descriptor<SizeClass>(ptr);
        /*
        if (__likely(desc->thread_id() == thread_id())) {
            auto size = desc->deallocate_local(ptr);
            auto state = desc->state_.load(std::memory_order_relaxed);
            if (state & DescriptorCached)
                return;

            if (state & DescriptorGlobal) {
                cleanup_descriptor(desc, desc->size());
            } else {
                state = desc->state_.load(std::memory_order_acquire);
                if (state & DescriptorGlobal) {
                    cleanup_descriptor(desc, desc->size());
                }
            }

            return;
        }

        auto size = desc->deallocate_global(ptr);
        auto state = desc->state_.load(std::memory_order_acquire);
        if (state & DescriptorCached)
            return;

        if (!(state & DescriptorGlobal))
            desc_.supdate_descriptor_state(desc, DescriptorGlobal);

        cleanup_descriptor(desc, size);
        */

        auto size = desc->deallocate(ptr);
        cleanup_descriptor<SizeClass>(desc, size);
    }

    template< size_t SizeClass > void cleanup_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc, std::size_t size) {

        if(__unlikely(size == desc->capacity() - 1)) {
            uint64_t state = 0;
            if ((state = update_descriptor_state(desc, DescriptorQueued))) {
                // TODO: why?
                if (state & DescriptorFull)
                    push_descriptor<SizeClass>(desc);
                // TODO: need to check counter
                if (read_descriptor_state(desc) == DescriptorDone)
                    deallocate_descriptor<SizeClass>(desc);
            }
        } else if(__unlikely(size == 0)) {
            uint64_t state = 0;
            if ((state = update_descriptor_state(desc, DescriptorEmpty)) == DescriptorDone) {
                deallocate_descriptor<SizeClass>(desc);
            }
        }
    }

    template< size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* get_cached_descriptor() {
        auto size = size_class_offset(SizeClass);
        assert(size_class_cache_[size]);
        return (arena_descriptor< ArenaSize, SizeClass >*)size_class_cache_[size];
    }

    template< size_t SizeClass > void initialize_cached_descriptor() {
        auto size = size_class_offset(SizeClass);
        if (!size_class_cache_[size])
            reset_cached_descriptor<SizeClass>();
    }

    template< size_t SizeClass > arena_descriptor<ArenaSize, SizeClass>* reset_cached_descriptor() {
        auto size = size_class_offset(SizeClass);

    again:
        uint32_t index;
        uint64_t state = DescriptorQueued;
        while(size_classes_[size].pop(index)) {
            auto* desc = (arena_descriptor<ArenaSize, SizeClass>*)descriptor_manager_.get_descriptor(index);
            // TODO: is validate still needed? I believe it was needed when descriptors
            // were not erased before reusing
            if ((validate_descriptor_state< SizeClass >(desc) && desc->size() != desc->capacity())) {
                init_descriptor_state(desc, DescriptorCached);
                size_class_cache_[size] = desc;
                return desc;
            }
        }

        auto* desc = allocate_descriptor<SizeClass>();
        assert(desc->size() == 0);
        init_descriptor_state(desc, DescriptorCached);
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
        return desc;
    }

    template< size_t SizeClass > void push_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
        (void)desc;
        auto size = size_class_offset(SizeClass);
        assert(get_cached_descriptor<SizeClass>() != desc);
        // TODO: a case where descriptor is in a heap, but we cross the size - 1
        // TODO: not thread-safe
        if (!size_classes_[size].get(descriptor_manager_.get_descriptor_index(desc)))
            size_classes_[size].push(descriptor_manager_.get_descriptor_index(desc));
    }

    template< size_t SizeClass > void deallocate_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
        if (size_classes_[size_class_offset(SizeClass)].erase(descriptor_manager_.get_descriptor_index(desc))) {
            segment_manager_.deallocate_segment(desc->begin());
            descriptor_manager_.deallocate_descriptor(desc);
        }
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

    // Global
    static descriptor_manager<std::array<uint8_t, DescriptorSize >, MaxSize / ArenaSize, PageSize> descriptor_manager_;

    // Local
    static segment_manager<PageSize, ArenaSize, MaxSize> segment_manager_;

    // Local
    static std::array< detail::elastic_atomic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> size_classes_;
    // Thread-local
    static std::array<arena_descriptor_base*, 23> size_class_cache_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> segment_manager<PageSize, ArenaSize, MaxSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::segment_manager_;

template < std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> descriptor_manager<std::array<uint8_t, DescriptorSize>, MaxSize / ArenaSize, PageSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::descriptor_manager_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array< detail::elastic_atomic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_classes_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<arena_descriptor_base*, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_class_cache_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::atomic<uint64_t> arena_allocator_base<PageSize, ArenaSize, MaxSize>::counter_;

template <typename T > class allocator
    : public arena_allocator_base< 1<<21, 1<<17, 1ull<<40 >
{
    template <typename U> friend class allocator;

public:
    using value_type    = T;

    allocator() noexcept {
        initialize_cached_descriptor<size_class<T>()>();
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
