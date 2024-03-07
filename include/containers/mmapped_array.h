//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <memory>

#if defined(__linux__)
#include <sys/mman.h>
#endif

namespace containers {

#if defined(__linux__)
    template< typename T, size_t Capacity = 1 << 30 > class mmapped_array {
        static constexpr size_t capacity_ = Capacity;
        std::atomic<size_t> size_ = 0;
        void* data_ = nullptr;

        T* at(size_t n) {
            T* ptr = reinterpret_cast<T*>(data_) + n;
            assert((reinterpret_cast<uintptr_t>(ptr) & (alignof(T) - 1)) == 0);
            return ptr;
        }

        T& read(size_t size, size_t n) {
            assert(n < size);
            (void)size;
            T* ptr = at(n);
            return *ptr;
        }

    public:
        using value_type = T;

        class reader_state {
            template< typename, size_t > friend class mmapped_array;
            size_t size;
        };

        mmapped_array() {
            data_ = mmap(0, capacity_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if ((uintptr_t)data_ == (uintptr_t)-1)
                std::abort();

            madvise(data_, capacity_, MADV_WILLNEED);
        }

        ~mmapped_array() { 
            if (!std::is_trivially_destructible_v<T>) {
                size_t size = size_.load(std::memory_order_relaxed);
                if (size) {
                    do {
                        at(--size)->~T();
                    } while(size);
                }
            }    
            munmap(data_, capacity_);
        }

        template< typename... Args > size_t emplace_back(Args&&... args) {
            size_t size = size_.load(std::memory_order_relaxed);
            new(reinterpret_cast<T*>(data_) + size) T{std::forward<Args>(args)...};
            size_.store(size + 1, std::memory_order_release);
            return size + 1;
        }


        size_t push_back(const T& value) { return emplace_back(value); }
        size_t push_back(T&& value) { return emplace_back(std::move(value)); }
    
        const T& operator[](size_t n) const {
            return const_cast<mmapped_array<T>&>(*this)->operator[](n);
        }

        T& operator[](size_t n) {
            return read(size_.load(std::memory_order_acquire), n);
        }

        T& read(reader_state& state, size_t n) {
            if (n >= state.size)
                state.size = size_.load(std::memory_order_acquire);
            return read(state.size, n);
        }

    };

#endif

}