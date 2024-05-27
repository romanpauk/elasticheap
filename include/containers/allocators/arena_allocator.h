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
#endif

namespace containers {

template< typename T > struct arena_allocator_traits {
    static constexpr std::size_t header_size() { return 32; }
};

template< typename Allocator = std::allocator<uint8_t> > class arena
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits = std::allocator_traits< allocator_type >;
    
    static constexpr std::size_t BlockSize = 1 << 20;
    static_assert((BlockSize & (BlockSize - 1)) == 0);

    struct block {
        block* next;
        std::size_t size:63;
        std::size_t owned:1;
    };

    static_assert(sizeof(block) == 16);

    block* block_ = nullptr;
    uintptr_t block_ptr_ = 0;
    uintptr_t block_end_ = 0;
    
    static uint64_t round_up(uint64_t n) {
    #if defined(_WIN32)
        return n == 1 ? 1 : 1 << (64 - _lzcnt_u64(n - 1));
    #else
        return n == 1 ? 1 : 1 << (64 - __builtin_clzl(n - 1));
    #endif
    }

    block* allocate_block(std::size_t size) {
        block *ptr = reinterpret_cast<block*>(allocator_traits::allocate(*this, size));
        assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(block) - 1)) == 0);
        return ptr;
    }

    void deallocate_block(block* ptr) {
        assert(ptr->owned);
        allocator_traits::deallocate(*this, reinterpret_cast<uint8_t*>(ptr), ptr->size);
    }

    void request_block(std::size_t bytes) {
        std::size_t size = round_up(std::max(BlockSize, sizeof(block) + bytes + arena_allocator_traits< allocator_type >::header_size()));
        assert((size & (size - 1)) == 0);
        size -= arena_allocator_traits< allocator_type >::header_size();
        assert((size - sizeof(block)) >= bytes);
        auto head = allocate_block(size);
        head->owned = true;
        head->size = size;
        push_block(head);
    }

    void push_block(block* head) {
        head->next = block_;
        block_ = head;
        block_ptr_ = reinterpret_cast<uintptr_t>(block_) + sizeof(block);
        block_end_ = reinterpret_cast<uintptr_t>(block_) + block_->size;
    }
    
public:
    arena() = default;

    template< typename T, std::size_t N > arena(T(&buffer)[N])
        : arena(reinterpret_cast<uint8_t*>(buffer), N * sizeof(T)) {
        static_assert(std::is_trivial_v<T>);
        static_assert(N * sizeof(T) > sizeof(block));
    }

    arena(uint8_t* buffer, std::size_t size) {
        assert(size > sizeof(block));
        auto head = reinterpret_cast<block*>(buffer);
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~arena() {
        auto head = block_;
        while(head) {
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

    template< std::size_t Alignment > uintptr_t allocate(const std::size_t bytes) {
    again:
        const uintptr_t offset = uintptr_t(block_ptr_ + Alignment - 1) & ~(Alignment - 1);
        if (offset + bytes > block_end_) {
            request_block(bytes);
            goto again;
        }

        assert((offset & (Alignment - 1)) == 0);
        block_ptr_ = offset + bytes;
        return offset;
    }
    
    static constexpr std::size_t header_size() { return sizeof(block); }
};

template <typename T, typename Arena = arena<> > class arena_allocator {
    template <typename U, typename ArenaU> friend class arena_allocator;
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

    void deallocate(value_type*, std::size_t) noexcept {}
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