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
#include <vector>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <deque>
#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace containers {

#if defined(__linux__)
    template< typename T, size_t Capacity = 1 << 30 > class mmapped_array {
        static constexpr size_t capacity_ = Capacity;
        size_t size_ = 0;
        void* data_ = nullptr;

    public:
        ~mmapped_array() { munmap(data_, capacity_); }

        template< typename Ty > void push_back(Ty&& value) {
            if (!data_) {
                data_ = mmap(0, capacity_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if ((uintptr_t)data_ == -1)
                    std::abort();
            }

            new(reinterpret_cast<T*>(data_) + size_++) T(std::forward<Ty>(value));
        }
    };
#endif

    template< typename T, typename Allocator = std::allocator<T>, size_t BlockByteSize = 4096, size_t BlocksGrowSize = 16 >
    class growable_array: Allocator {
        struct block_trivially_destructible {
            static_assert(std::is_trivially_destructible_v<T>);

            static constexpr size_t capacity() {
                static_assert(BlockByteSize >= sizeof(size_t));
                return std::max(size_t((BlockByteSize - sizeof(size_t)) / sizeof(T)), size_t(1));
            }

            template< typename Ty > void push_back(Ty&& value) {
                assert(size_ < capacity());
                new (at(size_++)) T(std::forward<Ty>(value));
            }

            T& operator[](size_t n) {
                assert(n < size_);
                return *at(n);
            }

            size_t size() const { return size_; }

        private:
            T* at(size_t n) {
                uint8_t* ptr = storage_.data() + n * sizeof(T);
                assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
                return reinterpret_cast<T*>(ptr);
            }

            std::array<uint8_t, sizeof(T) * capacity()> storage_;
            size_t size_ = 0;
        };

        struct block_destructible: block_trivially_destructible {
            static_assert(std::is_trivially_destructible_v<T>);

            ~block_destructible() {
                if (size_ > 0) {
                    do {
                        this->at(--size_)->~T();
                    } while (size_);
                }
            }
        };

        using block = std::conditional_t< std::is_trivially_destructible_v<T>, block_trivially_destructible, block_destructible >;

        alignas(64) std::atomic<size_t> size_ = 0;

        alignas(64) block** map_ = nullptr;

        alignas(64) size_t map_size_ = 0;
        size_t map_capacity_ = 0;
        std::deque< std::pair<block**, size_t>,
            typename std::allocator_traits<Allocator>::template rebind_alloc< std::pair<block**, size_t> > > retired_maps_;

        template< typename U > U* allocate(size_t n) {
            typename std::allocator_traits<Allocator>::template rebind_alloc<U> allocator(*this);
            U* ptr = allocator.allocate(n);
            allocator.construct(ptr);
            return ptr;
        }

        template< typename U > void deallocate(U* ptr, size_t n) {
            typename std::allocator_traits<Allocator>::template rebind_alloc<U> allocator(*this);
            allocator.deallocate(ptr, n);
        }

    public:
        using value_type = T;

        growable_array()
            : retired_maps_(*this)
        {}

        ~growable_array() {
            clear();
        }

        void clear() {
            if (map_size_ > 0) {
                do {
                    --map_size_;
                    if (!std::is_trivially_destructible_v<block>)
                        map_[map_size_]->~block();
                    deallocate<block>(map_[map_size_], 1);
                } while (map_size_);
                
                deallocate<block*>(map_, map_capacity_);
                map_ = nullptr;
                map_capacity_ = 0;

                for (auto& [map, size] : retired_maps_) {
                    deallocate<block*>(map, size);
                }
                retired_maps_.clear();
            }
        }

        const T& operator[](size_t n) const {
            return const_cast<growable_array<T>&>(*this)->operator[](n);
        }

        T& operator[](size_t n) {
            size_t size = size_.load(std::memory_order_acquire);
            assert(n < size);
            auto index = n / block::capacity();
            auto offset = n - index * block::capacity();
            return (*map_[index])[offset];
        }

        size_t size() const { return size_.load(std::memory_order_relaxed); }

        template< typename Ty > void push_back(Ty&& value) {
            if (map_) {
                assert(map_size_ > 0);
                if (map_[map_size_ - 1]->size() < block::capacity()) {
            insert:
                    map_[map_size_ - 1]->push_back(std::forward<Ty>(value));
                    size_.store(size_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
                } else {
                    if (map_size_ < map_capacity_) {
                        map_[map_size_++] = allocate<block>(1);
                        goto insert;
                    } else {
                        auto map = allocate<block*>(map_capacity_ * BlocksGrowSize);
                        std::memcpy(map, map_, sizeof(block*) * map_capacity_);
                        retired_maps_.emplace_back(map_, map_capacity_);
                        map_ = map;
                        map_capacity_ *= BlocksGrowSize;
                        map_[map_size_++] = allocate<block>(1);
                        goto insert;
                    }
                }
            } else {
                map_ = allocate<block*>(BlocksGrowSize);
                map_[0] = allocate<block>(1);
                map_size_ = 1;
                map_capacity_ = BlocksGrowSize;
                goto insert;
            }
        }
    };
}
