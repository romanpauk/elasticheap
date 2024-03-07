//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/lockfree/detail/aligned.h>

#include <atomic>
#include <array>

#if defined(_WIN32)
#include <intrin.h>
#endif

#if defined(__linux__)
#include <x86intrin.h>
#endif

namespace containers {
    template< typename T, size_t N > struct counter {
        void inc(T value, size_t index) { 
            values_[index].v += value;
        }

        T get() {
            _mm_mfence();
            T result = 0;
            for(auto& value: values_) 
                result += value.v;
            return result;
        }

    private:
        struct value {
            T v{};
        };
        std::array< detail::aligned< value >, N > values_{}; 
    };

    template< typename T, size_t N > struct counter< std::atomic< T >, N > {
        void inc(T value, size_t index) { 
            values_[index].fetch_add(value, std::memory_order_relaxed); 
        }

        T get() { 
            std::atomic_thread_fence(std::memory_order_acquire);
            T result = 0;
            for(auto& value: values_) 
                result += value.load(std::memory_order_relaxed);
            return result;
        }

    private:
        struct value {
            T value;
        };
        std::array< detail::aligned< std::atomic< T > >, N > values_{}; 
    };
}
