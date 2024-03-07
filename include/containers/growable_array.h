//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cstring>

namespace containers {
    namespace detail {
        template< typename... Args > struct compressed_tuple: Args... {
            compressed_tuple() = default;
            template<typename Arg> compressed_tuple(Arg&& arg): Args(arg)... {}

            template< size_t N > auto& get() {
                return static_cast< typename std::tuple_element<N, std::tuple<Args...> >::type& >(*this);
            }
        };

        constexpr size_t log2(size_t n) { return ((n<2) ? 1 : 1 + log2(n/2)); }

        template< typename T, size_t BlockSize > struct growable_array_block {
            template< typename... Args > void emplace(T* ptr, Args&&... args) {
                new (ptr) T{std::forward<Args>(args)...};
            }

            T& operator[](size_t n) {
                return *at(n);
            }

            T* begin() { return at(0); }

            void destroy(size_t size) {
                if (size > 0) {
                    do {
                        this->at(--size)->~T();
                    } while (size);
                }
            }

        protected:
            T* at(size_t n) {
                T* ptr = reinterpret_cast<T*>(storage_.data()) + n;
                assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
                return ptr;
            }

            std::array<uint8_t, sizeof(T) * BlockSize> storage_;
        };
    };

    // Single writer, multiple readers dynamic append-only array.
    template< 
        typename T, 
        typename Allocator = std::allocator<T>, 
        size_t BlockSize = 128, 
        size_t BlocksGrowFactor = 8,
        typename Block = detail::growable_array_block<T, BlockSize> >
    class growable_array: detail::compressed_tuple<
        typename std::allocator_traits<Allocator>::template rebind_alloc<Block>,
        typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t>
    > {
        static_assert((BlockSize & (BlockSize - 1)) == 0);
            
        using block_type = Block;

        struct block_map {
            block_map* next = 0;
            size_t capacity_ = 0;
            block_type* blocks[0];
        };

        template< typename U > struct stack {
            void push(U* value) {
                assert(value);
                value->next = head_.next;
                head_.next = value;
            }

            U* top() {
                return head_.next; 
            }

            U* pop() {
                U* value = head_.next;
                head_.next = value ? value->next : nullptr;
                return value;
            }

        private:
            U head_;
        };

        std::atomic<size_t> size_ = 0;
        size_t map_size_ = 0;
        stack<block_map> maps_;

        auto& block_allocator() { return this->template get<0>(); }
        block_type* allocate_block() { return block_allocator().allocate(1); }
        void deallocate_block(block_type* ptr) { return block_allocator().deallocate(ptr, 1); }
        
        auto& byte_allocator() { return this->template get<1>(); }
        block_map* allocate_block_map(size_t n) { 
            return (block_map*)byte_allocator().allocate(sizeof(block_map) + sizeof(block_type*) * n);
        }
        void deallocate_block_map(block_map* ptr, size_t n) { 
            byte_allocator().deallocate((uint8_t*)ptr, sizeof(block_map) + sizeof(block_type*) * n);
        }

        T& read(size_t size, size_t n) {
            assert(n < size);
            assert(maps_.top());
            (void)size;
            auto index = n >> (detail::log2(BlockSize) - 1);
            auto offset = n & (BlockSize - 1);
            return (*maps_.top()->blocks[index])[offset];
        }
    public:
        using value_type = T;
        
        class reader_state {
            template< typename U, typename AllocatorU, size_t, size_t, typename > friend class growable_array;
            size_t size = 0;
        };

        growable_array() = default;
        growable_array(Allocator allocator): detail::compressed_tuple<
            typename std::allocator_traits<Allocator>::template rebind_alloc<Block>,
            typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t>
        >(allocator) {} 

        growable_array(const growable_array&) = delete;
        growable_array& operator = (const growable_array&) = delete;
      
        ~growable_array() {
            clear();
        }
        
        void clear() {
            if (map_size_ > 0) {
                auto map = maps_.pop();
                assert(map);
                auto size = size_.exchange(0, std::memory_order_relaxed);
                do {
                    --map_size_;
                    if (!std::is_trivially_destructible_v<T>) {
                        auto count = size & (BlockSize - 1);
                        map->blocks[map_size_]->destroy(count);
                        size -= count;
                    }
                    deallocate_block(map->blocks[map_size_]);
                } while (map_size_);
                
                deallocate_block_map(map, map->capacity_);
                map_size_ = 0;
                
                while ((map = maps_.pop())) {
                    deallocate_block_map(map, map->capacity_);
                }
            }
        }

        const T& operator[](size_t n) const {
            return const_cast<growable_array<T>&>(*this)->operator[](n);
        }

        T& operator[](size_t n) {
            return read(size_.load(std::memory_order_acquire), n);
        }

        T& read(reader_state& state, size_t n) {
            if (n >= state.size)
                state.size = size_.load(std::memory_order_acquire);
            return read(state.size, n);
        }

        size_t size() const { return size_.load(std::memory_order_acquire); }
        size_t empty() const { return size_.load(std::memory_order_acquire) == 0; }

        template< typename... Args > size_t emplace_back(Args&&... args) {
            size_t size = size_.load(std::memory_order_relaxed); 
            size_t index = size >> (detail::log2(BlockSize) - 1);
            size_t offset = size & (BlockSize - 1);

            if (maps_.top()) {
                assert(map_size_ > 0);
                if (index < map_size_) {
                insert:
                    maps_.top()->blocks[index]->emplace(
                        maps_.top()->blocks[index]->begin() + offset, std::forward<Args>(args)...);
                    size_.store(size + 1, std::memory_order_release);
                    return size + 1;
                } else if (map_size_ < maps_.top()->capacity_) {
                    maps_.top()->blocks[map_size_++] = allocate_block();
                    goto insert;
                } else {
                    auto capacity = maps_.top()->capacity_;
                    auto map = allocate_block_map(capacity * BlocksGrowFactor);
                    std::memcpy(map->blocks, maps_.top()->blocks, sizeof(block_type*) * capacity);
                    map->blocks[map_size_++] = allocate_block();
                    map->capacity_ = capacity * BlocksGrowFactor;
                    maps_.push(map);
                    goto insert;
                }
            } else {
                auto map = allocate_block_map(BlocksGrowFactor);
                map->blocks[0] = allocate_block();
                map_size_ = 1;
                map->capacity_ = BlocksGrowFactor;
                maps_.push(map);
                goto insert;
            }
        }

        size_t push_back(const T& value) { return emplace_back(value); }
        size_t push_back(T&& value) { return emplace_back(std::move(value)); }
    };
}
