//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/hyaline_allocator.h>
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
        typename Allocator = detail::hyaline_allocator< T >,
        typename Backoff = detail::exponential_backoff<>
    > class unbounded_stack
    {
        struct node
        {
            template< typename... Args > node(node* n, Args&&... args)
                : next(n), value(std::forward< Args >(args)...)
            {}

            ~node() { if constexpr (!std::is_trivially_destructible_v< T >) value.reset(); }

            node* next;
            detail::optional< T > value;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;
        alignas(64) std::atomic< node* > head_{};

    public:
        using value_type = T;

        unbounded_stack() = default;
        
        ~unbounded_stack()
        {
            // TODO: this is destroy_and_deallocate()
            clear(head_.load(std::memory_order_acquire), &allocator_type::deallocate);
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            auto head = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, head, head_.load(), std::forward< Args >(args)...);

            Backoff backoff;
            while (true)
            {
                if(head_.compare_exchange_weak(head->next, head))
                    break;

                backoff();
            }
            return true;
        }

        bool push(T&& value) { return emplace(std::move(value)); }
        bool push(const T& value) { return emplace(value); }

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

        bool empty() const { return head_.load() == nullptr; }

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
        void clear(node* head, void (allocator_type::*deallocate)(node*, size_t))
        {
            while (head)
            {
                auto next = head->next;
                (allocator_.*deallocate)(head, 1);
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
        typename Allocator = detail::hyaline_allocator< T >,
        typename Backoff = detail::exponential_backoff<>,
        typename InnerStack = bounded_stack_base< T, 1 << 16, Backoff, static_cast<uint32_t>(-1) >
    > class unbounded_blocked_stack
    {
        struct node
        {
            node() = default;
            node(node* n): next(n) {}

            node* next{};
            InnerStack stack;
        };

        using allocator_type = typename Allocator::template rebind< node >::other;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        mutable allocator_type allocator_;
        alignas(64) std::atomic< node* > head_{};

    public:
        using value_type = T;

        unbounded_blocked_stack()
        {
            auto head = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, head);
            head_.store(head);
        }

        ~unbounded_blocked_stack()
        {
            // TODO: this really is destroy_and_deallocate()...
            clear(head_.load(), &allocator_type::deallocate);
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_acquire);
                if (head->stack.emplace(std::forward< Args >(args)...))
                    return true;

                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (top.index == -1)
                {
                    if(head_.compare_exchange_strong(head, head->next, std::memory_order_release))
                        allocator_.retire(head);
                }
                else
                {
                    auto new_head = allocator_traits_type::allocate(allocator_, 1);
                    allocator_traits_type::construct(allocator_, new_head, head);

                    if (!head_.compare_exchange_strong(new_head->next, new_head, std::memory_order_release)) {
                        allocator_traits_type::destroy(allocator_, new_head);
                        allocator_traits_type::deallocate(allocator_, new_head, 1);
                    }
                }
            }
        }

        bool push(T&& value) { return emplace(std::move(value)); }
        bool push(const T& value) { return emplace(value); }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_acquire);
                if (!head)
                    return false;

                if (head->stack.pop(value))
                    return true;

                if (!head->next)
                    return false;

                auto top = head->stack.top_.load(std::memory_order_relaxed);
                if (top.index == 0)
                {
                    if (head->stack.top_.compare_exchange_strong(top, { (uint32_t)-1, top.counter + 1, T{} }))
                    {
                        if (head_.compare_exchange_strong(head, head->next, std::memory_order_release))
                            allocator_.retire(head);
                    }
                }
                else if (top.index == -1)
                {
                    if (head_.compare_exchange_strong(head, head->next, std::memory_order_release))
                        allocator_.retire(head);
                }
            }
        }

        bool empty() const
        {
            auto guard = allocator_.guard();
            auto head = allocator_.protect(head_, std::memory_order_acquire);
            return head->stack.empty();
        }
        
        void clear()
        {
            Backoff backoff;
            auto head = head_.load(std::memory_order_relaxed);
            while (head && !head_.compare_exchange_weak(head, nullptr))
                backoff();
            clear(head, &allocator_type::retire);

            // TODO: push/pop assume there is always non-null head
        }

    private:
        void clear(node* head, void (allocator_type::*deallocate)(node*, size_t))
        {
            while (head)
            {
                auto next = head->next;
                // TODO: this is either retire or deallocate. For retire, we can't call dtor,
                // for deallocate, we should...
                (allocator_.*deallocate)(head, 1);
                head = next;
            }
        }
    };
}
