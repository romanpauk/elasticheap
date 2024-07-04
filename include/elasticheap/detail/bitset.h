//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

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
        static_assert(false);
/*
        void clear() {
            for(size_t i = 0; i < Size; ++i)
                bytes_[i] = 0;
        }

        void set(size_t index) {
            assert(index < Bits);
            bytes_[index/sizeof(T)] |= (1 << (index & (sizeof(T) - 1));
        }

        void clear(size_t index) {
            assert(index < Bits);
            bytes_[index/sizeof(T)] &= ~(1 << (index & (sizeof(T) - 1)));
        }

        bool get(size_t index) {
            assert(index < Bits);
            return bytes_[index/sizeof(T)] & (1 << (index & (sizeof(T) - 1)));
        }

        bool empty() const {
            for(size_t i = 0; i < Size; ++i)
                if (bytes_[i] != 0) return false;
            return true;
        }

        bool full() const {
            if constexpr (Bits & 7) {
                for(size_t i = 0; i < Size - 1; ++i)
                    if (bytes_[i] != ~T{0}) return false;

                // TODO:
            } else {
                for(size_t i = 0; i < Size; ++i)
                    if (bytes_[i] != ~T{0}) return false;
            }
            
            return true;
        }

    private:
        T values_[Size];
    */
    };
    
    template<
        std::size_t Bits, 
        typename T
    > struct bitset_base<Bits, T, 1> {
        static_assert((Bits & (Bits - 1)) == 0);

        using value_type = T;
    
        void clear() { value_ = 0; }
        
        void set(std::size_t index) {
            assert(index < Bits);
            value_ |= 1 << index;
        }

        void clear(std::size_t index) {
            assert(index < Bits);
            value_ &= ~(1 << index);
        }

        bool get(std::size_t index) {
            assert(index < Bits);
            return value_ & (1 << index);
        }

        bool empty() const { return value_ == 0; }
        bool full() const { return value_ == std::numeric_limits<T>::max(); }
        
    private:
        T value_;
    };

    template< std::size_t Bits > struct bitset
        : bitset_base< Bits > {};
}