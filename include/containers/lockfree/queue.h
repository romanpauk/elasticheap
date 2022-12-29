//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/detail/hazard_era_allocator.h>

#include <atomic>
#include <memory>

// Simple, fast, and practical non-blocking and blocking concurrent queue algorithms.
// http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf

namespace containers
{
    template< typename T > struct queue_node
    {
        T value;
        std::atomic< queue_node< T >* > next;
    };

    template < typename T, typename Allocator = hazard_era_allocator< queue_node< T >, 2 > > class queue
    {
    private:
        Allocator allocator_;

        alignas(64) std::atomic< queue_node< T >* > head_;
        alignas(64) std::atomic< queue_node< T >* > tail_;

    public:
        queue()
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
            auto n = allocator_.allocate(std::forward< Ty >(value), nullptr);
            while(true)
            {
                auto tail = allocator_.template protect<0>(tail_, std::memory_order_relaxed);
                auto next = allocator_.template protect<1>(tail->next, std::memory_order_relaxed);
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
            }
        }

        bool pop(T& value)
        {
            while (true)
            {
                auto head = allocator_.template protect<0>(head_, std::memory_order_relaxed);
                auto next = allocator_.template protect<1>(head->next, std::memory_order_relaxed);
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
                            allocator_.deallocate(head);
                            return true;
                        }
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
