//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/hazard_era_allocator.h>

#include <atomic>
#include <memory>

namespace containers
{
    // Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
    // http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf
    template < typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class queue
    {
        struct queue_node
        {
            T value;
            std::atomic< queue_node* > next;
        };

        using allocator_type = typename Allocator::template rebind< queue_node >::other;
        allocator_type& allocator_;
        
        alignas(64) std::atomic< queue_node* > head_;
        alignas(64) std::atomic< queue_node* > tail_;

    public:
        queue(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            auto n = allocator_.allocate();
            n->next = nullptr;
            head_.store(n, std::memory_order_relaxed);
            tail_.store(n, std::memory_order_relaxed);
        }

        ~queue()
        {
            clear();
        }
        
        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            auto n = allocator_.allocate(std::forward< Ty >(value), nullptr);
            Backoff backoff;
            while(true)
            {
                // TODO: could this benefit from protecting multiple variables in one call?
                auto tail = allocator_.protect(tail_, std::memory_order_relaxed);
                auto next = allocator_.protect(tail->next, std::memory_order_relaxed);
                if (tail == tail_.load())
                {
                    if (next == nullptr)
                    {
                        if (tail->next.compare_exchange_weak(next, n))
                        {
                            tail_.compare_exchange_weak(tail, n);
                            break;
                        }
                        else
                            backoff();
                    }
                    else
                    {
                        tail_.compare_exchange_weak(tail, next);
                    }
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            Backoff backoff;
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                auto next = allocator_.protect(head->next, std::memory_order_relaxed);
                auto tail = tail_.load();
                if (head == head_.load())
                {
                    if (head == tail)
                    {
                        if(next == nullptr)
                            return false;

                        tail_.compare_exchange_weak(tail, next);
                    }
                    else
                    {
                        value = next->value;
                        if (head_.compare_exchange_weak(head, next))
                        {
                            allocator_.retire(head);
                            return true;
                        }
                        else
                            backoff();
                    }
                }
            }
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next.load();
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };
}
