//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#if defined(_WIN32)
#include <emmintrin.h>
#include <containers/lockfree/detail/atomic16_win32.h>
#else
#include <atomic>
#endif

namespace containers
{
#if defined(_WIN32)
    template< typename T > using atomic16 = detail::atomic16< T >;
#else
    // TODO: need to make sure loads/stores are not using cmpexchg16b
    template< typename T > using atomic16 = std::atomic< T >;
#endif
}
