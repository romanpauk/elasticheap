//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <type_traits>

namespace containers::detail
{
    template< typename T > class optional
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
}
