//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/hazard_era_allocator.h>
#include <containers/lockfree/detail/optional.h>

#include <atomic>
#include <memory>

namespace containers
{
    //
    // Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
    // http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
    //
    template <
        typename T,
        typename Allocator = detail::hazard_era_allocator< T >,
        typename Backoff = detail::exponential_backoff<>
    > class unbounded_queue
    {
        struct node
        {
            ~node() { value.reset(); }

            std::atomic< node* > next{};
            detail::optional< T > value;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< node* > head_;
        alignas(64) std::atomic< node* > tail_;

    public:
        using value_type = T;

        unbounded_queue(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            auto n = allocator_.allocate();
            head_.store(n);
            tail_.store(n);
        }

        ~unbounded_queue()
        {
            clear();
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            auto guard = allocator_.guard();
            auto n = allocator_.allocate(nullptr, T{std::forward< Args >(args)...});
            Backoff backoff;
            while (true)
            {
                // TODO: could this benefit from protecting multiple variables in one call?
                auto tail = allocator_.protect(tail_);
                auto next = allocator_.protect(tail->next);
                if (tail == tail_.load())
                {
                    if (next == nullptr)
                    {
                        if (tail->next.compare_exchange_weak(next, n))
                        {
                            tail_.compare_exchange_weak(tail, n);
                            break;
                        }
                    }
                    else
                    {
                        tail_.compare_exchange_weak(tail, next);
                    }
                }

                backoff();
            }
        }

        void push(const T& value) { return emplace(value); }
        void push(T&& value) { return emplace(std::move(value)); }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            Backoff backoff;
            while (true)
            {
                auto head = allocator_.protect(head_);
                auto tail = tail_.load();
                auto next = allocator_.protect(head->next);
                if (head == head_.load())
                {
                    if (head == tail)
                    {
                        if (next == nullptr)
                            return false;

                        tail_.compare_exchange_weak(tail, next);
                    }
                    else
                    {
                        value = next->value.value();

                        if (head_.compare_exchange_weak(head, next))
                        {
                            allocator_.retire(head);
                            return true;
                        }
                    }
                }

                backoff();
            }
        }

        bool empty() const
        {
            return head_.load() == tail_.load();
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next.load();
                allocator_.deallocate(head);
                head = next;
            }
        }
    };
}
