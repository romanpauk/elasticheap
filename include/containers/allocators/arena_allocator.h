//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <memory>

#if defined(_WIN32)
#include <immintrin.h>
#else
#include <sys/mman.h>
#endif

namespace containers {

template< typename Allocator = std::allocator<char> > class arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits = std::allocator_traits< allocator_type >;
    
    static constexpr std::size_t BlockSize = 1 << 21;
    static_assert((BlockSize & (BlockSize - 1)) == 0);

    struct block {
        std::size_t size:63;
        std::size_t owned:1;
        block* next;
    };

    static_assert(sizeof(block) == 16);

    block* head_ = nullptr;
    uintptr_t block_address_ = 0;
    std::size_t block_size_ = 0;

    static uint64_t round_up(uint64_t n) {
    #if defined(_WIN32)
        return n == 1 ? 1 : 1 << (64 - _lzcnt_u64(n - 1));
    #else
        return n == 1 ? 1 : 1 << (64 - __builtin_clzl(n - 1));
    #endif
    }

    block* allocate_block(std::size_t size) {
        return reinterpret_cast<block*>(allocator_traits::allocate(*this, size));
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

    void request_block(std::size_t size) {
        std::size_t block_size = round_up(std::max(BlockSize, sizeof(block) + size));
        assert((block_size & (block_size - 1)) == 0);
        auto head = allocate_block(block_size);
        head->owned = true;
        head->size = block_size;
        head->next = head_;
        init_head(head);
    }

    void init_head(block* head) {
        block_address_ = reinterpret_cast<uintptr_t>(head) + sizeof(block);
        block_size_ = head->size - sizeof(block);
        head_ = head;
    }
    
public:
    arena() = default;

    arena(uint8_t* buffer, std::size_t size) {
        assert(size > sizeof(block));
        auto head = reinterpret_cast<block*>(buffer);
        head->owned = false;
        head->size = size;
        head->next = nullptr;
        init_head(head);
    }

    ~arena() {
        auto head = head_;
        while(head) {
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

    template< std::size_t Alignment > uintptr_t allocate(std::size_t bytes) {
    again:
        uintptr_t block_offset = uintptr_t(block_address_ + Alignment - 1) & ~(Alignment - 1);
        if (block_offset + bytes - block_address_ > block_size_) {
            request_block(bytes);
            goto again;
        }

        block_address_ = block_offset + bytes;
        return block_offset;
    }
};

template <typename T, typename Arena = arena<> > class arena_allocator {
    Arena* arena_ = nullptr;
public:
    using value_type    = T;

    arena_allocator(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator(const arena_allocator<U, Arena>& other) noexcept
        : arena_(other.arena_) {}
        
    value_type* allocate(std::size_t n) {
        return reinterpret_cast<value_type*>(arena_->template allocate<alignof(T)>(sizeof(T) * n));
    }

    void deallocate(value_type* p, std::size_t) noexcept {}
};

template <typename T, typename U, typename Arena>
bool operator == (const arena_allocator<T, Arena>& lhs, const arena_allocator<U, Arena>& rhs) noexcept {
    return lhs.arena_ = rhs.arena_;
}

template <typename T, typename U, typename Arena>
bool operator != (const arena_allocator<T, Arena>& x, const arena_allocator<U, Arena>& y) noexcept {
    return !(x == y);
}

}