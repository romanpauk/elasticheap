//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/hazard_era_allocator.h>
#include <containers/lockfree/detail/aligned.h>

#include <atomic>
#include <memory>
#include <iostream>

namespace containers
{
    // https://en.wikipedia.org/wiki/Treiber_stack
    template< typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class stack
    {
        struct stack_node
        {
            T value;
            stack_node* next;
        };

        using allocator_type = typename Allocator::template rebind< stack_node >::other;
        allocator_type& allocator_;

        alignas(64) std::atomic< stack_node* > head_;

    public:
        stack(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast< allocator_type* >(&allocator))
        {}

        ~stack()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            Backoff backoff;
            while (!head_.compare_exchange_weak(head->next, head))
                backoff();
        }

        bool pop(T& value)
        {
            Backoff backoff;
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                {
                    return false;
                }
                
                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value);
                    allocator_.retire(head);
                    return true;
                }
                else
                    backoff();
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

    /*
    // A Scalable Lock-free Stack Algorithm - https://people.csail.mit.edu/shanir/publications/Lock_Free.pdf
    template< typename T, typename Allocator = hazard_era_allocator< T >, typename Backoff = exp_backoff<> > class stack_eb
    {
        struct stack_node
        {
            T value;
            stack_node* next;
        };

        using allocator_type = typename Allocator::template rebind< stack_node >::other;
        allocator_type allocator_;

        struct operation
        {
            size_t thread_id;
            bool push;
            stack_node* node;
        };

        hazard_era_allocator< operation > op_allocator_;
        
        static constexpr int32_t collision_freq = 4;
        static_assert(is_power_of_2< collision_freq >::value);

        alignas(64) std::array< aligned< std::atomic< operation* > >, thread::max_threads > location_;
        alignas(64) std::array< aligned< std::atomic< size_t > >, thread::max_threads / 4 > collision_;
        alignas(64) std::array< aligned< std::atomic< int32_t > >, thread::max_threads > collision_count_;
        alignas(64) std::array< aligned< std::atomic< uint64_t > >, thread::max_threads > collision_area_;
        alignas(64) std::array< aligned< std::atomic< uint32_t > >, thread::max_threads > spin_;
        alignas(64) std::atomic< stack_node* > head_;

    public:
        stack_eb(Allocator& allocator = Allocator::instance())
            : allocator_(*reinterpret_cast<allocator_type*>(&allocator))
        {}

        ~stack_eb()
        {
            clear();
        }

        template< typename Ty > void push(Ty&& value)
        {
            auto guard = allocator_.guard();
            auto head = allocator_.allocate(std::forward< Ty >(value), head_.load(std::memory_order_relaxed));
            while (true)
            {
                if(head_.compare_exchange_weak(head->next, head))
                {
                   return;
                }
                else
                {
                    auto op = op_allocator_.allocate(thread_id(), true, head);
                    op_allocator_.protect(op);
                    if (collide(op))
                        return;
                    op_allocator_.retire(op);
                }
            }
        }

        bool pop(T& value)
        {
            auto guard = allocator_.guard();
            while (true)
            {
                auto head = allocator_.protect(head_, std::memory_order_relaxed);
                if (!head)
                    return false;
                
                if (head_.compare_exchange_weak(head, head->next))
                {
                    value = std::move(head->value);
                    allocator_.retire(head);
                    return true;
                }
                else
                {
                    operation op{ thread_id(), false, nullptr };
                    if (collide(&op))
                    {
                        value = std::move(op.node->value);
                        allocator_.deallocate_unsafe(op.node);
                        return true;
                    }
                }
            }
        }

    private:
        size_t thread_id() { return allocator_.thread_id(); }

        void backoff_update(bool success)
        {
            if (success)
            {
                if(collision_count_[thread_id()].fetch_add(1, std::memory_order_relaxed) == collision_freq)
                {
                    collision_count_[thread_id()].store(0, std::memory_order_relaxed);

                    auto spin = spin_[thread_id()].load(std::memory_order_relaxed);
                    spin_[thread_id()].store(spin * 2, std::memory_order_relaxed);
                }
            }
            else
            {
                if (collision_count_[thread_id()].fetch_sub(1, std::memory_order_relaxed) == -collision_freq)
                {
                    collision_count_[thread_id()].store(0, std::memory_order_relaxed);

                    auto spin = std::max(uint32_t(2), spin_[thread_id()].load(std::memory_order_relaxed));
                    spin_[thread_id()].store(spin / 2, std::memory_order_relaxed);
                }
            }
        }

        bool collide(operation* op)
        {
            Backoff backoff;
            assert(location_[op->thread_id].load() == 0);
            location_[op->thread_id].store(op);
            auto pos = op->thread_id & (collision_.size() - 1);
            auto other_id = collision_[pos].load(std::memory_order_relaxed);
            while(!collision_[pos].compare_exchange_weak(other_id, op->thread_id))
                backoff();
            if (other_id)
            {
                auto other_op = op_allocator_.protect(location_[other_id]);
                if (other_op && other_op->thread_id == other_id && other_op->push != op->push)
                {
                    auto p = op;
                    if (location_[op->thread_id].compare_exchange_strong(p, nullptr))
                    {
                        bool result = collide(op, other_op);
                        backoff_update(result);
                        assert(location_[op->thread_id].load() == 0);
                        return result;
                    }
                    else
                    {
                        if (!op->push)
                        {
                            assert(p);
                            assert(p->push);
                            assert(p->node);
                            op->node = p->node;
                            location_[op->thread_id].store(nullptr);
                            op_allocator_.retire(p);
                        }

                        backoff_update(true);
                        assert(location_[op->thread_id].load() == 0);
                        return true;
                    }
                }
            }

            auto spin = 1024; //spin_[op->thread_id].load(std::memory_order_relaxed);
            while(spin--) _mm_pause();

            auto p = op;
            if (!location_[op->thread_id].compare_exchange_strong(p, nullptr))
            {
                if (!op->push)
                {
                    assert(p);
                    assert(p->push);
                    assert(p->node);
                    op->node = p->node;
                    location_[op->thread_id].store(nullptr);
                    op_allocator_.retire(p);
                }

                backoff_update(true);
                assert(location_[op->thread_id].load() == 0);
                return true;
            }
            else
            {
                backoff_update(false);
                assert(location_[op->thread_id].load() == 0);
                return false;
            }
        }

        bool collide(operation* op, operation* other_op)
        {
            assert(op);
            assert(other_op);
            assert(op != other_op);
            assert(op->thread_id != other_op->thread_id);
            assert(op->push != other_op->push);

            assert(location_[op->thread_id].load() == 0);

            if (op->push)
            {
                return location_[other_op->thread_id].compare_exchange_strong(other_op, op);
            }
            else
            {
                if (location_[other_op->thread_id].compare_exchange_strong(other_op, nullptr))
                {
                    assert(other_op->node);
                    op->node = other_op->node;
                    op_allocator_.retire(other_op);
                    return true;
                }
                else
                {
                    return false;
                }
            }
        }

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
    */
}
