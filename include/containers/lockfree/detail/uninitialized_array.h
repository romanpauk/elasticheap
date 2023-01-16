//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

namespace containers::detail
{
    template< typename T, size_t N > class uninitialized_array
    {
    public:
        T& operator[](size_t n) { return *reinterpret_cast<T*>(&data_[n]); }
        
    private:
        alignas(T) std::array< std::aligned_storage_t< sizeof(T), alignof(T) >, N > data_;
    };
}
