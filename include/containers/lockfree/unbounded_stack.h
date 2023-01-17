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
#include <containers/lockfree/bounded_stack.h>

#include <atomic>
#include <memory>

namespace containers
{
    //
    // https://en.wikipedia.org/wiki/Treiber_stack
    //
    template<
        typename T,
        typename Allocator = detail::hazard_era_allocator< T >,
        typename Backoff = detail::exponential_backoff<>
    > class unbounded_stack
    {
        struct node
        {
            ~node() { value.reset(); }

            node* next;
            detail::optional< T > value;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< node* > head_{};

    public:
        using value_type = T;

        unbounded_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {}

        ~unbounded_stack()
        {
            clear(head_.load(std::memory_order_acquire), &allocator_type::deallocate);
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            auto head = allocator_.allocate(head_.load(), std::forward< Args >(args)...);
            Backoff backoff;
            while (true)
            {
                if(head_.compare_exchange_weak(head->next, head))
                    break;

                backoff();
            }
        }

        void push(T&& value) { emplace(std::move(value)); }
        void push(const T& value) { emplace(value); }

        bool pop(T& value)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_);
                if (!head)
                {
                    return false;
                }

                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value.value());
                    allocator_.retire(head);
                    return true;
                }

                backoff();
            }
        }

        bool empty() const { return head_.load(); }

        void clear()
        {
            Backoff backoff;
            node* null = nullptr;
            auto head = head_.load();
            while (head && !head_.compare_exchange_weak(head, nullptr))
                backoff();
            clear(head, &allocator_type::retire);
        }

    private:
        void clear(node* head, void (allocator_type::*deallocate)(node*))
        {
            while (head)
            {
                auto next = head->next;
                (allocator_.*deallocate)(head);
                head = next;
            }
        }
    };

    //
    // This algorithm just marks a block for deletion. If either pop or push observe failure
    // when working with the block, they mark it. Whoever sees marked block, tries to remove it.
    // So all are working on sequence of operations (block fine -> marked -> removed).
    //
    // With N=128, we can run hazard_era_reclamation with every allocation/deallocation
    // without performance impact.
    //
    template<
        typename T,
        typename Allocator = detail::hazard_era_allocator< T >,
        typename Backoff = detail::exponential_backoff<>,
        typename InnerStack = bounded_stack_base< T, 128, Backoff, static_cast<uint32_t>(-1) >
    > class unbounded_blocked_stack
    {
        struct node
        {
            node* next{};
            InnerStack stack;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< node* > head_{};

    public:
        using value_type = T;

        unbounded_blocked_stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {
            head_ = allocator_.allocate();
        }

        ~unbounded_blocked_stack()
        {
            clear(head_.load(), &allocator_type::deallocate);
        }

        template< typename... Args > void emplace(Args&&... args)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_);
                auto top = head->stack.top_.load();
                if (head->stack.emplace(std::forward< Args >(args)...))
                    return;

                if (top.index == -1)
                {
                    if(head_.compare_exchange_strong(head, head->next))
                    {
                        allocator_.retire(head);
                        continue;
                    }
                }
                else
                {
                    auto new_head = allocator_.allocate(head);
                    if (!head_.compare_exchange_strong(new_head->next, new_head))
                        allocator_.deallocate(new_head);
                }

                backoff();
            }
        }

        void push(T&& value) { emplace(std::move(value)); }
        void push(const T& value) { emplace(value); }

        bool pop(T& value)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_);
                if (!head)
                    return false;

                auto top = head->stack.top_.load();
                if (head->stack.pop(value))
                    return true;

                if (!head->next)
                    return false;

                if (top.index == -1 || head->stack.top_.compare_exchange_strong(top, { (uint32_t)-1, top.counter + 1, T{} }))
                {
                    if (head_.compare_exchange_strong(head, head->next))
                    {
                        allocator_.retire(head);
                        continue;
                    }
                }

                backoff();
            }
        }

        /* TODO:
        bool empty() const
        {
            auto guard = allocator_.guard();
            auto head = allocator_.protect(head_, std::memory_order_acquire);
            return head->stack.empty();
        }
        */

        void clear()
        {
            Backoff backoff;
            node* null = nullptr;
            auto head = head_.load();
            while (head && !head_.compare_exchange_weak(head, nullptr))
                backoff();
            clear(head, &allocator_type::retire);

            // TODO: push/pop assume there is always non-null head
        }

    private:
        void clear(node* head, void (allocator_type::*deallocate)(node*))
        {
            while (head)
            {
                auto next = head->next;
                (allocator_.*deallocate)(head);
                head = next;
            }
        }
    };
}
