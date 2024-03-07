//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#if defined(_WIN32)
#include <emmintrin.h>
#include <containers/lockfree/detail/atomic16_win32.h>
#else
#include <atomic>
#endif

#include <algorithm>

namespace containers
{
#if defined(_WIN32)
    template< typename T > using atomic16 = detail::atomic16< T >;
#else
    // TODO: need to make sure loads/stores are not using cmpexchg16b
    template< typename T > using atomic16 = std::atomic< T >;
#endif

    // Atomic minimum/maximum - https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0493r3.pdf
    template < typename U >
    U atomic_fetch_max_explicit(std::atomic<U>* pv, typename std::atomic<U>::value_type v, std::memory_order m = std::memory_order_seq_cst) noexcept
    {
        auto t = pv->load(m);
        while (std::max(v, t) != t) {
            if (pv->compare_exchange_weak(t, v, m, m))
                break;
        }
        return t;
    }
}
