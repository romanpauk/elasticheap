//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#if defined(_WIN32)
#include <atomic>
#include <emmintrin.h>

namespace containers::detail
{
    template< typename T > bool cas16(T& result, T& expected, const T& desired)
    {
        static_assert(sizeof(T) == 16);
        __int64 lo = *((__int64*)&desired + 0);
        __int64 hi = *((__int64*)&desired + 1);
        return _InterlockedCompareExchange128((__int64*)&result, hi, lo, (__int64*)&expected);
    }

    // Assumes AVX loads/stores are atomic when aligned to 16bytes
    template< typename T > struct atomic16
    {
        static_assert(sizeof(T) == 16);
        alignas(16) T value_;

        static void fence(std::memory_order mo)
        {
            switch (mo)
            {
            case std::memory_order_relaxed:
            case std::memory_order_acquire:
            case std::memory_order_release:
            case std::memory_order_seq_cst:
                std::atomic_thread_fence(mo);
                return;
            default:
                std::abort();
            }
        }

    public:
        using value_type = T;

        T load(std::memory_order mo = std::memory_order_seq_cst) const
        {
            fence(mo);
            __m128i value = _mm_load_si128((__m128i*)&value_);
            return *reinterpret_cast<T*>(&value);
        }

        void store(const T& value, std::memory_order mo = std::memory_order_seq_cst)
        {
            _mm_store_si128((__m128i*)&value_, _mm_load_si128((__m128i*)&value));
            fence(mo);
        }

        bool compare_exchange_strong(T& expected, const T& desired, std::memory_order = std::memory_order_seq_cst)
        {
            // This has its own fence in _InterlockedCompare
            return cas16(value_, expected, desired);
        }

        bool compare_exchange_weak(T& expected, const T& desired, std::memory_order = std::memory_order_seq_cst)
        {
            return compare_exchange_strong(expected, desired);
        }
    };
}
#endif
