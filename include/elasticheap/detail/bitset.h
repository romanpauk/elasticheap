//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <immintrin.h>

namespace elasticheap::detail {
    template< std::size_t Bits > struct bitset_type {
        using type = std::conditional_t< Bits <= 8, uint8_t,
            std::conditional_t< Bits <= 16, uint16_t,
            std::conditional_t< Bits <= 32, uint32_t,
            uint64_t
        >>>;
    };

    template<
        std::size_t Bits, 
        typename T = typename bitset_type<Bits>::type,
        std::size_t Size = (Bits + sizeof(T) * 8 - 1) / (sizeof(T) * 8)
    > struct bitset_base {
        static_assert(Size > 1);
        static_assert((Bits & (Bits - 1)) == 0);
        
        using value_type = T;
        static constexpr std::size_t size() { return Bits; }

        void clear() {
            for(std::size_t i = 0; i < Size; ++i)
                values_[i] = 0;
        }

        void set(std::size_t index) {
            assert(index < Bits);
            values_[index/sizeof(T)/8] |= (T{1} << (index & (sizeof(T) * 8 - 1)));
        }

        void clear(std::size_t index) {
            assert(index < Bits);
            values_[index/sizeof(T)/8] &= ~(T{1} << (index & (sizeof(T) * 8 - 1)));
        }

        bool get(std::size_t index) const {
            assert(index < Bits);
            return values_[index/sizeof(T)/8] & (T{1} << (index & (sizeof(T) * 8 - 1)));
        }

        bool empty() const {
            for(std::size_t i = 0; i < Size; ++i)
                if (values_[i] != 0) return false;
            return true;
        }

        bool full() const {
            for(std::size_t i = 0; i < Size; ++i)
                if (values_[i] != std::numeric_limits<T>::max()) return false;            
            return true;
        }

        std::size_t find_first() const {
            for(std::size_t i = 0; i < Size; ++i) {
                if (values_[i])
                    return _tzcnt_u64(values_[i]) + i * sizeof(T) * 8;
            }
            return Size;
        }

        std::size_t pop_first() {
            for(std::size_t i = 0; i < Size; ++i) {
                if (values_[i]) {
                    auto count = _tzcnt_u64(values_[i]);
                    values_[i] &= ~(T{1} << count);
                    return count + i * sizeof(T) * 8;
                }
            }
            return Size;
        }

    private:
        T values_[Size];
    };
    
    template<
        std::size_t Bits, 
        typename T
    > struct bitset_base<Bits, T, 1> {
        static_assert((Bits & (Bits - 1)) == 0);

        using value_type = T;
        static constexpr std::size_t size() { return Bits; }
        
        void clear() { value_ = 0; }
        
        void set(std::size_t index) {
            assert(index < Bits);
            value_ |= T{1} << index;
        }

        void clear(std::size_t index) {
            assert(index < Bits);
            value_ &= ~(T{1} << index);
        }

        bool get(std::size_t index) const {
            assert(index < Bits);
            return value_ & (T{1} << index);
        }

        bool empty() const { return value_ == 0; }
        bool full() const { return value_ == std::numeric_limits<T>::max(); }
        
        std::size_t find_first() const {
            return _tzcnt_u64(value_);
        }

        operator T() const { return value_; }

    private:
        T value_;
    };

    template< std::size_t Bits > struct bitset
        : bitset_base< Bits > {};
}