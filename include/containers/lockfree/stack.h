//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/detail/hazard_era_allocator.h>

#include <atomic>
#include <memory>

// https://en.wikipedia.org/wiki/Treiber_stack

namespace containers
{
    template< typename T > struct stack_node
    {
        T value;
        stack_node< T >* next;
    };

    template< typename T, typename Allocator = hazard_era_allocator< stack_node< int >, 1 > > class stack
    {        
        Allocator allocator_;

        alignas(64) std::atomic< stack_node< T >* > head_;

    public:
        ~stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            while (!head_.compare_exchange_weak(head->next, head)) _mm_pause();
        }

        bool pop(T& value)
        {
            while (true)
            {
                auto head = allocator_.template protect<0>(head_, std::memory_order_relaxed);
                if (!head)
                    return false;
                
                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value);
                    allocator_.deallocate(head);
                    return true;
                }
            }
        }

    private:
        void clear()
        {
            auto head = head_.load();
            while (head)
            {
                auto next = head->next;
                allocator_.deallocate_unsafe(head);
                head = next;
            }
        }
    };
}
