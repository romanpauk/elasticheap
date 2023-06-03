//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <utility>

#include <stdlib.h>

namespace containers::detail
{
    template< typename T, size_t Alignment = 64 > struct alignas(Alignment) aligned : T
    {
        aligned() = default;
        template< typename... Args > aligned(Args&&... value) : T{ std::forward< Args >(value)... } {}

    private:
        static_assert((Alignment & (Alignment - 1)) == 0);
        char padding[Alignment - sizeof(T) % Alignment];
    };
}
