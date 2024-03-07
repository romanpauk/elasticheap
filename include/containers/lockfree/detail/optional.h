//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <type_traits>

namespace containers::detail
{
    template< typename T > struct is_trivial: std::is_trivial< T > {};
    template< typename T > constexpr auto is_trivial_v = is_trivial< T >::value;

    template< typename T, bool IsTrivial = is_trivial_v< T > > class optional;

    template< typename T > class optional< T, false >
    {
    public:
        optional() = default;

        template< typename... Args > optional(Args&&... args)
        {
            emplace(std::forward< Args >(args)...);
        }

        T& value()
        {
            return *reinterpret_cast<T*>(&data_);
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            new(&value()) T{ std::forward< Args >(args)... };
        }

        void reset()
        {
            value().~T();
        }

    private:
        std::aligned_storage_t< sizeof(T), alignof(T) > data_;
    };

    template< typename T > class optional< T, true >
    {
    public:
        optional() = default;

        template< typename... Args > optional(Args&&... args)
        {
            emplace(std::forward< Args >(args)...);
        }

        T& value()
        {
            return data_;
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            data_ = T{ std::forward< Args >(args)... };
        }
        
    private:
        T data_;
    };
}
