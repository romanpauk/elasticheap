//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <memory>
#include <limits>

#if defined(_WIN32)
#include <immintrin.h>
#endif

namespace containers {

template< typename T, std::size_t BlockSize > class arena2 { 
    static_assert((BlockSize & (BlockSize - 1)) == 0);

    static constexpr std::size_t BlockElements = (BlockSize - 16 - 4)/(sizeof(T) * 2);
    static_assert(BlockElements <= std::numeric_limits<uint16_t>::max());

public:
    uintptr_t ptr_;
    uintptr_t end_;
    uint32_t  free_list_size_;
    uint16_t  free_list_[BlockElements];

    uintptr_t allocate() {
        if (free_list_size_) {
            const uintptr_t begin = reinterpret_cast< uintptr_t >(this) + sizeof(*this);
            uint16_t index = free_list_[free_list_size_];
            return begin + index * sizeof(T);
        }
    
        uintptr_t ptr = ptr_;
        if (ptr + sizeof(T) > end_) {
            return 0;
        }

        assert((ptr & (alignof(T) - 1)) == 0);
        ptr_ += sizeof(T);
        return ptr;
    }
    
    void deallocate(uintptr_t ptr) {
        if (ptr + sizeof(T) == ptr_) {
            ptr_ -= sizeof(T);
        } else {
            const uintptr_t begin = reinterpret_cast< uintptr_t >(this) + sizeof(*this);
            assert(begin <= ptr && ptr < end_);
            const size_t index = (ptr - begin) / sizeof(T);
            assert(free_list_size_ < BlockElements);
            free_list_[free_list_size_++] = index;
        }
    }
};

template <typename T, typename Allocator = std::allocator<void> > class arena_allocator2
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    template <typename U, typename AllocatorU> friend class arena_allocator2;
    using block_allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using block_allocator_traits = std::allocator_traits< block_allocator_type >;

    static constexpr std::size_t BlockSize = 1 << 18; // 262k
    static_assert((BlockSize & (BlockSize - 1)) == 0);
    
    struct block {
        block* next;
        arena2<T, BlockSize> arena;
    };

    block* block_ = nullptr;

    void request_block() {
        auto head = allocate_block();
        head->next = block_;
        block_ = head;
    }

    block* allocate_block() {
        block *ptr = reinterpret_cast<block*>(block_allocator_traits::allocate(*this, BlockSize));
        assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(block) - 1)) == 0);
        ptr->arena.ptr_ = reinterpret_cast<uintptr_t>(ptr) + sizeof(block);
        ptr->arena.end_ = reinterpret_cast<uintptr_t>(ptr) + BlockSize;
        ptr->arena.free_list_size_ = 0;
        return ptr;
    }

    void deallocate_block(block* ptr) {
        block_allocator_traits::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), BlockSize);
    }
    
public:
    using value_type    = T;

    arena_allocator2() noexcept {
        request_block();
    }

    ~arena_allocator2() {
        auto head = block_;
        while(head) {
            auto next = head->next;
            deallocate_block(head);
            head = next;
        }
    }
    
    value_type* allocate(std::size_t n) {
        assert(n == 1);
        (void)n;
        auto ptr = reinterpret_cast<value_type*>(block_->arena.allocate());
        if (!ptr) {
            request_block();
            ptr = reinterpret_cast<value_type*>(block_->arena.allocate());
        }
        return ptr;
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        assert(n == 1);
        (void)n;
        block_->arena.deallocate(reinterpret_cast<uintptr_t>(ptr));
    }
};

template <typename T, typename U, typename Arena>
bool operator == (const arena_allocator2<T, Arena>& lhs, const arena_allocator2<U, Arena>& rhs) noexcept {
    return lhs.arena_ = rhs.arena_;
}

template <typename T, typename U, typename Arena>
bool operator != (const arena_allocator2<T, Arena>& x, const arena_allocator2<U, Arena>& y) noexcept {
    return !(x == y);
}

}