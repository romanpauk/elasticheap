//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

namespace containers::detail
{
    template< size_t Size > struct padding
    {
    private:
        char pad[Size]{0};
    };

    template<> struct padding<0> {};
}
