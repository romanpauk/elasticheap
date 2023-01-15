//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <emmintrin.h>

namespace containers::detail
{
    template< size_t Initial = 256, size_t Max = 65536 > struct exponential_backoff
    {
        static_assert(Initial % 2 == 0);
        static_assert(Max % 2 == 0);

        void operator() ()
        {
            size_t iters = spin();
            while (iters--)
                _mm_pause();
        }

        size_t spin()
        {
            if ((state_ <<= 1) >= Max)
                state_ = Max;
            return state_;
        }

        size_t state() const { return state_; }

    private:
        size_t state_ = Initial;
    };
}
