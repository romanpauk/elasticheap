//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace elasticheap::detail {
    template< std::size_t Bits > struct atomic_bitset_type {
        using type = std::conditional_t< Bits <= 8, uint8_t,
            std::conditional_t< Bits <= 16, uint16_t,
            std::conditional_t< Bits <= 32, uint32_t,
            uint64_t
        >>>;
    };

    template<
        std::size_t Bits,
        typename T = typename atomic_bitset_type<Bits>::type,
        std::size_t Size = (Bits + sizeof(T) * 8 - 1) / (sizeof(T) * 8)
    > struct atomic_bitset_base {
        static_assert(Size > 1);
        static_assert((Bits & (Bits - 1)) == 0);

        using value_type = std::atomic<T>;
        static constexpr std::size_t size() { return Bits; }

        void clear(std::memory_order order = std::memory_order_relaxed) {
            for(std::size_t i = 0; i < Size; ++i)
                values_[i].store(0, std::memory_order_relaxed);
            std::atomic_thread_fence(order);
        }

        bool set(std::size_t index, std::memory_order order = std::memory_order_relaxed) {
            assert(index < Bits);
            auto value = T{1} << (index & (sizeof(T) * 8 - 1));
            return (values_[index/sizeof(T)/8].fetch_or(value, order) & value) == 0;
        }

        bool clear(std::size_t index, std::memory_order order = std::memory_order_relaxed) {
            assert(index < Bits);
            auto value = T{1} << (index & (sizeof(T) * 8 - 1));
            return values_[index/sizeof(T)/8].fetch_and(~value, order) & value;
        }

        bool get(std::size_t index, std::memory_order order = std::memory_order_relaxed) const {
            assert(index < Bits);
            auto value = T{1} << (index & (sizeof(T) * 8 - 1));
            return values_[index/sizeof(T)/8].load(order) & value;
        }

        bool empty(std::memory_order order = std::memory_order_relaxed) const {
            std::atomic_thread_fence(order);
            for(std::size_t i = 0; i < Size; ++i)
                if (values_[i].load(std::memory_order_relaxed) != 0) return false;
            return true;
        }

        bool full(std::memory_order order = std::memory_order_relaxed) const {
            std::atomic_thread_fence(order);
            for(std::size_t i = 0; i < Size; ++i)
                if (values_[i].load(std::memory_order_relaxed) != std::numeric_limits<T>::max()) return false;
            return true;
        }

    private:
        std::array< std::atomic<T>, Size > values_;
    };

    template<
        std::size_t Bits,
        typename T
    > struct atomic_bitset_base<Bits, T, 1> {
        static_assert((Bits & (Bits - 1)) == 0);

        using value_type = std::atomic<T>;
        static constexpr std::size_t size() { return Bits; }
        static constexpr T full_value() { return std::numeric_limits<T>::max(); }

        void clear(std::memory_order order = std::memory_order_relaxed) { value_.store(0, order); }

        T set(std::size_t index, std::memory_order order = std::memory_order_relaxed) {
            assert(index < Bits);
            return value_.fetch_or(T{1} << index, order);
        }

        T clear(std::size_t index, std::memory_order order = std::memory_order_relaxed) {
            assert(index < Bits);
            return value_.fetch_and(~(T{1} << index), order);
        }

        bool get(std::size_t index, std::memory_order order = std::memory_order_relaxed) const {
            assert(index < Bits);
            return value_.load(order) & (T{1} << index);
        }

        bool empty(std::memory_order order = std::memory_order_relaxed) const {
            return value_.load(order) == T{0};
        }

        bool full(std::memory_order order = std::memory_order_relaxed) const {
            return value_.load(order) == full_value();
        }

        static std::size_t popcount(T value) {
            return _mm_popcnt_u64(value);
        }

    private:
        std::atomic<T> value_;
    };

    template< std::size_t Bits > struct atomic_bitset
        : atomic_bitset_base< Bits > {};
}
