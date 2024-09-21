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

static thread_local uint64_t thread_token;
inline uint64_t thread_id() {
    return (uint64_t)&thread_token;
}

enum {
    DescriptorNone = 0,
    DescriptorCached = 1,
    DescriptorUncached = 2,
    DescriptorQueued = 4,
};

static constexpr std::size_t DescriptorSize = 1<<16;

// Freelist size:
// (1<<17)/4*2 = 65535
//
template< std::size_t ArenaSize, std::size_t SizeClass, std::size_t Alignment = 8 > struct arena_descriptor {
    static constexpr std::size_t Capacity = ArenaSize/SizeClass;

    arena_descriptor(void* buffer) {
        thread_id_ = thread_id();
        begin_ = (uint8_t*)buffer;
        size_class_ = SizeClass;
        local_size_ = Capacity;
        local_size_atomic_.store(Capacity, std::memory_order_relaxed);

        for (size_t i = 0; i < Capacity; ++i)
            local_list_[i] = i;

        shared_size_.store(0, std::memory_order_relaxed);
    }

    void* allocate_local() {
        assert(verify(thread_id()));
        uint16_t index = local_list_[--local_size_];
        assert(index < Capacity);
        uint8_t* ptr = begin_ + index * SizeClass;
        assert(is_ptr_valid(ptr));
        local_size_atomic_.store(local_size_, std::memory_order_release);
        return ptr;
    }

    void deallocate_local(void* ptr) {
        assert(verify(thread_id()));
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / SizeClass;
        assert(index < Capacity);
        local_list_[local_size_++] = index;
        local_size_atomic_.store(local_size_, std::memory_order_release);
    }

    void* allocate_shared() {
        assert(verify());
        size_t index = shared_bitset_.pop_first();
        assert(index < Capacity);
        void* ptr = begin_ + index * SizeClass;
        shared_size_.fetch_sub(1, std::memory_order_release);
        return ptr;
    }

    void deallocate_shared(void* ptr) {
        assert(verify());
        assert(thread_id_ != thread_id());
        assert(is_ptr_valid(ptr));
        size_t index = ((uint8_t*)ptr - begin_) / SizeClass;
        assert(index < Capacity);
        shared_bitset_.set(index);
        shared_size_.fetch_add(1, std::memory_order_release);
    }

    static constexpr std::size_t capacity() { return Capacity; }

    std::size_t size_local() const {
        return local_size_;
    }

    std::size_t size_shared() const {
        return shared_size_.load(std::memory_order_acquire);
    }

    std::size_t size() const {
        return local_size_atomic_.load(std::memory_order_acquire)
            + shared_size_.load(std::memory_order_acquire);
    }

    uint8_t* begin() { return begin_; }
    uint8_t* end() { return begin_ + ArenaSize; }

    bool is_ptr_valid(void* ptr) {
        assert(is_ptr_in_range(ptr, SizeClass, begin(), end()));
        assert(is_ptr_aligned(ptr, Alignment));
        return true;
    }

    bool verify(std::size_t thread_id = 0) {
    #if defined(MAGIC)
        assert(magic_ == 0xDEADBEEF);
    #endif
        assert(size_class_ == SizeClass);
        if (thread_id)
            assert(thread_id_ == thread_id);
        return true;
    }

#if defined(MAGIC)
    uint32_t magic_ = 0xDEADBEEF;
#endif
    uint64_t thread_id_;

    uint8_t* begin_;
    uint32_t size_class_;

    std::atomic<uint64_t> state_;

    uint32_t local_size_;
    std::atomic<uint64_t> local_size_atomic_;
    std::array< uint16_t, Capacity > local_list_;

    std::atomic<uint64_t> shared_size_;
    detail::atomic_bitset< round_up(Capacity) > shared_bitset_;
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
    page_manager< PageSize, MaxSize > page_manager_;
    descriptor_manager< page_descriptor, PageCount, MetadataPageSize > page_descriptors_;

    detail::elastic_atomic_bitset_heap< uint32_t, PageCount, MetadataPageSize > allocated_pages_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize > class arena_allocator_base {
    static_assert((PageSize & (PageSize - 1)) == 0);
    static_assert((ArenaSize & (ArenaSize - 1)) == 0);
    static_assert((MaxSize & (MaxSize - 1)) == 0);

    static constexpr std::size_t PageArenaCount = (PageSize)/(ArenaSize);

    static uint64_t get_version() {
        return version_.fetch_add(1, std::memory_order_relaxed);
    }

    static uint64_t get_version(uint64_t state) {
        return state >> 8;
    }

    static uint64_t make_state(uint64_t version, uint64_t state) {
        assert(state <= 0xFF);
        return (version << 8) | state;
    }

    template< typename T > static bool update_state(T* desc, uint64_t state, uint64_t update) {
        const auto version = get_version(state);
        for(;;) {
            assert(!(state & update));

            if (desc->state_.compare_exchange_strong(state, state | update, std::memory_order_release))
                return true;

            // If descriptor got reused, bail out
            if (get_version(state) != version)
                return false;

            // If someone else did the update, bail out
            if (state & update)
                return false;
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

        if (__likely(desc->size_local()))
            return desc->allocate_local();

        if (__likely(desc->size_shared()))
            return desc->allocate_shared();

        // Descriptor can't be destroyed before getting uncached
        auto version = get_version();
        desc->state_.store(make_state(version, DescriptorCached | DescriptorUncached), std::memory_order_release);

        // Descriptor can be destroyed now or going through destruction.
        // Try to deallocate it, if it fails, it means other threads have
        // not yet come to a deallocation phase.
        if (__unlikely(desc->size() == desc->capacity())) {
            deallocate_descriptor<SizeClass>(desc);
        }

        reset_cached_descriptor<SizeClass>();
        goto again;
    }

    template< size_t SizeClass > void deallocate_impl(void* ptr) noexcept {
        auto* desc = get_descriptor<SizeClass>(ptr);
        auto state = desc->state_.load(std::memory_order_acquire);

        if (desc->thread_id_ == thread_id()) {
            desc->deallocate_local(ptr);
        } else {
            desc->deallocate_shared(ptr);
        }

        // After size goes to 0, descriptor can be deallocated/reused at any time
        // Carefully use version to not modify it.

        if (__likely(!(state & DescriptorUncached)))
            return;

        if (__unlikely(!(state & DescriptorQueued))) {
            if (update_state(desc, state, DescriptorQueued)) {
                push_descriptor<SizeClass>(desc);
            }
        }

        // If the descriptor is reused/deallocated, this will either be a valid
        // destruction request, or no-op due to erase() in deallocate.
        if (__unlikely(desc->size() == desc->capacity())) {
            deallocate_descriptor<SizeClass>(desc);
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
        while(size_classes_[size].pop(index)) {
            auto* desc = (arena_descriptor<ArenaSize, SizeClass>*)descriptor_manager_.get_descriptor(index);
            assert(desc->size());
            desc->state_.store(DescriptorCached, std::memory_order_release);
            size_class_cache_[size] = desc;
            return desc;
        }

        auto* desc = allocate_descriptor<SizeClass>();
        assert(desc->size() == desc->capacity());
        desc->state_.store(DescriptorCached, std::memory_order_release);
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
        static_assert(sizeof(arena_descriptor<ArenaSize, SizeClass>) <= DescriptorSize);
        new(desc) arena_descriptor< ArenaSize, SizeClass >(buffer);
        return desc;
    }

    template< size_t SizeClass > void push_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
        auto size = size_class_offset(SizeClass);
        assert(get_cached_descriptor<SizeClass>() != desc);
        assert(!size_classes_[size].get(descriptor_manager_.get_descriptor_index(desc)));
        size_classes_[size].push(descriptor_manager_.get_descriptor_index(desc));
    }

    template< size_t SizeClass > void deallocate_descriptor(arena_descriptor<ArenaSize, SizeClass>* desc) {
    again:
        if (size_classes_[size_class_offset(SizeClass)].erase(descriptor_manager_.get_descriptor_index(desc))) {
            // The descriptor might have been reused and thus, erased by accident
            // If it become empty during time it was erased, put it back and retry.
            // If it is not empty after erase, someone just allocated. As it was
            // erased, it was not cached, put it back as it has to be the same size
            // class, too.
            if (desc->size() < desc->capacity()) {
                push_descriptor<SizeClass>(desc);
                if (desc->size() == desc->capacity())
                    goto again;
            }

            segment_manager_.deallocate_segment(desc->begin());
            descriptor_manager_.deallocate_descriptor(desc);
        }
    }

    //
    // TODO: use some more separated global/local state
    // Global       - shared
    // Local        - thread-local or cpu-local
    // Thread-local - thread-local
    //

    // Global
    // TODO: descriptors waste a lot of space, need a few descriptor classes
    static descriptor_manager<std::array<uint8_t, DescriptorSize>, MaxSize / ArenaSize, PageSize> descriptor_manager_;

    // Global
    static std::atomic<uint64_t> version_;

    // Global (should be CPU-local?)
    static segment_manager<PageSize, ArenaSize, MaxSize> segment_manager_;

    // Global (should be CPU-local?)
    static std::array< detail::elastic_atomic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> size_classes_;

    // Thread-local
    static std::array<void*, 23> size_class_cache_;
};

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> segment_manager<PageSize, ArenaSize, MaxSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::segment_manager_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::atomic< uint64_t > arena_allocator_base<PageSize, ArenaSize, MaxSize>::version_;

template < std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> descriptor_manager<std::array<uint8_t, DescriptorSize>, MaxSize / ArenaSize, PageSize> arena_allocator_base<PageSize, ArenaSize, MaxSize>::descriptor_manager_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array< detail::elastic_atomic_bitset_heap<uint32_t, MaxSize/ArenaSize, MetadataPageSize>, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_classes_;

template< std::size_t PageSize, std::size_t ArenaSize, std::size_t MaxSize> std::array<void*, 23> arena_allocator_base<PageSize, ArenaSize, MaxSize>::size_class_cache_;

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
