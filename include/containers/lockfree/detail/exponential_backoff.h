//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <emmintrin.h>
#include <cstdint>

namespace containers::detail {
    template< uint64_t Initial = 1<<16, uint64_t Max = 1<<24 > struct exponential_backoff {
        static_assert(Initial % 2 == 0);
        static_assert(Max % 2 == 0);

        exponential_backoff() = default;

        void operator() () {
            auto iters = spin();
            while (iters--)
                _mm_pause();
        }

        uint64_t spin() {
            auto jitter = (uint64_t)this;
            jitter ^= jitter >> 17;

            auto state = state_;
            if (state_ < Max)
                state_ <<= 1;
            return (uint64_t)jitter & (state - 1);
        }

        uint64_t state() const { return state_; }

    private:
        uint64_t state_ = Initial;
    };
}
