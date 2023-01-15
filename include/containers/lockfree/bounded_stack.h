//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/padding.h>
#include <containers/lockfree/atomic16.h>

#include <atomic>
#include <memory>
#include <cassert>

namespace containers
{
    //
    // Non-blocking Array-based Algorithms for Stack and Queues
    // https://link.springer.com/chapter/10.1007/978-3-540-92295-7_10
    //
    template<
        typename T,
        size_t Size,
        typename Backoff,
        uint32_t Mark = 0
    > struct bounded_stack_base
    {
        // By using paddings, make sure all the 16bytes will be zeroed.
        struct top_node
        {
            top_node() = default;
            top_node(uint32_t i, uint32_t c, T v)
                : index(i), counter(c), value(v)
            {}

            uint32_t index;
            uint32_t counter;
            T value;
            detail::padding<8 - sizeof(T)> p1;
        };

        struct array_node
        {
            array_node() = default;
            array_node(uint32_t c, T v)
                : counter(c), value(v)
            {}

            uint32_t counter;
            detail::padding<4> p1;
            T value;
            detail::padding<8 - sizeof(T)> p2;
        };

        static_assert(sizeof(top_node) == 16);
        static_assert(sizeof(array_node) == 16);
        static_assert(Size > 1);

        alignas(64) atomic16< top_node > top_{};
        alignas(64) std::array< atomic16< array_node >, Size + 1 > array_{};

        using value_type = T;

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto top = top_.load();

                // The article has finish() before if(top.index == empty/full), yet that worsens
                // pop() and push() scalability for empty and full stacks. As in those cases
                // push and pop have no effect, we can wait with finish till there will be someone
                // pushing to empty stack or popping from non empty stack.

                if (Mark && top.index == Mark)
                    return false;
                if (top.index == array_.size() - 1)
                    return false;

                finish(top);

                auto above_top = array_[top.index + 1].load();
                if (top_.compare_exchange_weak(top, top_node{ top.index + 1, above_top.counter + 1, T{ args... } }))
                    return true;

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
                auto top = top_.load();

                if (Mark && top.index == Mark)
                    return false;
                if (top.index == 0)
                    return false;              

                finish(top);

                auto below_top = array_[top.index - 1].load();
                if (top_.compare_exchange_weak(top, top_node{ top.index - 1, below_top.counter + 1, below_top.value }))
                {
                    value = std::move(top.value);
                    return true;
                }
                    
                backoff();
            }
        }

        static constexpr size_t capacity() { return Size - 1; }

        // TODO: bool empty() const;

    private:
        void finish(top_node& n)
        {
            assert(!Mark || n.index != Mark);
            auto top = array_[n.index].load();
            array_node expected { n.counter - 1, top.value };
            array_[n.index].compare_exchange_strong(expected, { n.counter, n.value });
        }
    };

    template<
        typename T,
        size_t Size,
        typename Backoff = detail::exponential_backoff<>
    > class bounded_stack
        : private bounded_stack_base< T, Size, Backoff >
    {
        using base_type = bounded_stack_base< T, Size, Backoff >;

    public:
        using value_type = typename base_type::value_type;

        using base_type::emplace;
        using base_type::push;
        using base_type::pop;
        using base_type::capacity;
        // TODO: using base_type::empty;
    };
}
