//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>

#include <atomic>
#include <memory>
#include <optional>

namespace containers
{
    // From a BBQ article
    template<
        typename T,
        size_t Size,
        typename Backoff = detail::exponential_backoff<>
    > class bounded_queue
    {
        static_assert(Size % 2 == 0);

        alignas(64) std::atomic< size_t > chead_{};
        alignas(64) std::atomic< size_t > ctail_{};
        alignas(64) std::atomic< size_t > phead_{};
        alignas(64) std::atomic< size_t > ptail_{};

        alignas(64) std::array< T, Size > values_;

    public:
        using value_type = T;

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto ph = phead_.load();
                auto pn = ph + 1;
                if (pn > ctail_.load() + Size)
                    return false;
                if (phead_.compare_exchange_strong(ph, pn))
                {
                    values_[pn & (Size - 1)] = T{ std::forward< Args >(args)... };
                    while (ptail_.load() != ph)
                        _mm_pause();
                    ptail_.store(pn);
                    return true;
                }

                backoff();
            }
        }

        bool push(const T& value) { return emplace(value); }
        bool push(T&& value) { return emplace(std::move(value)); }

        bool pop(T& value)
        {
            Backoff backoff;
            while (true)
            {
                auto ch = chead_.load();
                auto cn = ch + 1;
                if (cn > ptail_.load() + 1)
                    return false;
                if (chead_.compare_exchange_strong(ch, cn))
                {
                    value = std::move(values_[cn & (Size - 1)]);

                    while (ctail_.load() != ch)
                        _mm_pause();
                    ctail_.store(cn);
                    return true;
                }

                backoff();
            }
        }

        bool empty() const
        {
            return chead_.load() == ptail_.load();
        }

        static constexpr size_t capacity() { return Size; }
    };
}
