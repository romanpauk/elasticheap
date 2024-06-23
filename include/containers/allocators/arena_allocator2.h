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

template< typename T > struct arena_allocator_traits2 {
    static constexpr std::size_t header_size() { return 32; }
};

template< typename T, typename Allocator = std::allocator<uint8_t> > class arena2
    : std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>
{
    using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc<uint8_t>;
    using allocator_traits = std::allocator_traits< allocator_type >;
    
    static constexpr std::size_t BlockSize = 1 << 18; // 262k
    static_assert((BlockSize & (BlockSize - 1)) == 0);

    static constexpr std::size_t BlockElements = (BlockSize - 16 - 4)/(sizeof(T) * 2);
    static_assert(BlockElements <= std::numeric_limits<uint16_t>::max());

    struct block {
        block* next;
        std::size_t size:63;
        std::size_t owned:1;
        uint32_t    free_list_size = {0};
        uint16_t    free_list[BlockElements] = {0};
    };

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
        std::size_t size = round_up(std::max(BlockSize, sizeof(block) + bytes + arena_allocator_traits2< allocator_type >::header_size()));
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
    arena2() = default;

    template< typename U, std::size_t N > arena2(U(&buffer)[N])
        : arena2(reinterpret_cast<uint8_t*>(buffer), N * sizeof(U)) {
        static_assert(std::is_trivial_v<U>);
        static_assert(N * sizeof(U) > sizeof(block));
    }

    arena2(uint8_t* buffer, std::size_t size) {
        assert(size > sizeof(block));
        auto head = reinterpret_cast<block*>(buffer);
        head->owned = false;
        head->size = size;
        push_block(head);
    }

    ~arena2() {
        auto head = block_;
        while(head) {
            auto next = head->next;
            if (head->owned)
                deallocate_block(head);
            head = next;
        }
    }

    uintptr_t allocate(std::size_t n) {
        if (block_->free_list_size) {
            const uintptr_t begin = reinterpret_cast< uintptr_t >(block_) + sizeof(block);
            uint16_t index = block_->free_list[--block_->free_list_size];
            return begin + index * sizeof(T);
        }
            
    again:
        uintptr_t ptr = block_ptr_;
        if (ptr + sizeof(T) * n > block_end_) {
            //if (block_->free_list_size) {
            //    const uintptr_t begin = reinterpret_cast< uintptr_t >(block_) + sizeof(block);
            //    uint16_t index = block_->free_list[--block_->free_list_size];
            //    return begin + index * sizeof(T);
            //}
        
            request_block(sizeof(T) * n);
            goto again;
        }

        assert((ptr & (alignof(T) - 1)) == 0);
        block_ptr_ += sizeof(T) * n;
        return ptr;
    }
    
    void deallocate(uintptr_t ptr, std::size_t n) {
        if (ptr + sizeof(T) * n == block_ptr_) {
            block_ptr_ -= sizeof(T) * n;
        } else
        {
            const uintptr_t begin = reinterpret_cast< uintptr_t >(block_) + sizeof(block);
            const size_t index = (ptr - begin) / sizeof(T);
            assert(block_->free_list_size < BlockElements);
            block_->free_list[block_->free_list_size++] = index;
        }
    }

    static constexpr std::size_t header_size() { return sizeof(block); }
};

template <typename T, typename Arena = arena2<T> > class arena_allocator2 {
    template <typename U, typename ArenaU> friend class arena_allocator2;
    Arena* arena_ = nullptr;
public:
    using value_type    = T;

    arena_allocator2(Arena& arena) noexcept
        : arena_(&arena) {}

    template <typename U> arena_allocator2(const arena_allocator2<U, Arena>& other) noexcept
        : arena_(other.arena_) {}
        
    value_type* allocate(std::size_t n) {
        return reinterpret_cast<value_type*>(arena_->allocate(n));
    }

    void deallocate(value_type* ptr, std::size_t n) noexcept {
        arena_->deallocate(reinterpret_cast<uintptr_t>(ptr), n);
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