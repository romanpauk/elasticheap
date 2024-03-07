//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

// Hazard Eras - Non-Blocking Memory Reclamation - https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf
// Universal Wait-Free Memory Reclamation - https://arxiv.org/abs/2001.01999

#include <containers/lockfree/detail/aligned.h>
#include <containers/lockfree/detail/thread_manager.h>

#include <cassert>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

namespace containers::detail
{
    template< typename ThreadManager = thread > class hazard_era_allocator_base
    {
    public:
        struct hazard_buffer_header
        {
            uint64_t allocated;
            uint64_t retired;
        };

        // deleter is a static function with proper allocator type, so from any hazard_era_allocator<*>
        // 'correct' deallocation code is invoked on correctly aligned type to deallocate.
        // deleter usage for data in shared memory is problematic:
        //  1) obviously this would work only in same languages sharing the same destruction code. deleter
        //      would have to be replaced by type and the function address resolved for each process.
        //  2) node-based stacks and queues use destructor to invoke optional<T> destructor, yet if T is
        //      trivially destructible, there is nothing to invoke.

        using deleter = void (*)(hazard_buffer_header*);

        struct thread_data
        {
            uint64_t allocated;
            uint64_t retired;

            // TODO: retire list needs to be passed to someone upon thread exit
            std::vector< std::pair< hazard_buffer_header*, deleter > > retired_buffers;

            void clear()
            {
                allocated = retired = 0;
            }
        };
        
        alignas(64) std::array< detail::aligned< thread_data >, ThreadManager::max_threads > thread;

        struct thread_reservation
        {
            std::atomic< uint64_t > min_era;
            std::atomic< uint64_t > max_era;
        };

        alignas(64) std::atomic< uint64_t > era = 1;
        alignas(64) std::array< detail::aligned< thread_reservation >, ThreadManager::max_threads > reservations;

        struct thread_guard
        {
            ~thread_guard() { instance().clear_reservations(ThreadManager::id()); }
        };

        static hazard_era_allocator_base< ThreadManager >& instance()
        {
            static hazard_era_allocator_base< ThreadManager > instance;
            return instance;
        }

        thread_guard guard() { return thread_guard(); }

        size_t thread_id()
        {
            struct thread_destructor
            {
                ~thread_destructor() { instance().clear(ThreadManager::id()); }
            };

            // Register thread first, dtor second, so dtor runs before thread gets unregistered
            
            static thread_local size_t id = ThreadManager::id();
            static thread_local thread_destructor dtor;

            return id;
        }

        bool can_deallocate(hazard_buffer_header* buffer)
        {
            for (size_t tid = 0; tid < ThreadManager::max_threads; ++tid)
            {
                auto min_era = reservations[tid].min_era.load(std::memory_order_acquire);
                auto max_era = reservations[tid].max_era.load(std::memory_order_acquire);

                if (min_era <= buffer->allocated && buffer->allocated <= max_era)
                    return false;
                if (min_era <= buffer->retired && buffer->retired <= max_era)
                    return false;
                if (buffer->allocated <= min_era && buffer->retired >= max_era)
                    return false;
            }

            return true;
        }

        void cleanup()
        {
            auto& buffers = thread[thread_id()].retired_buffers;
            buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [this](const std::pair< hazard_buffer_header*, deleter >& p)
            {
                if (can_deallocate(p.first))
                {
                    p.second(p.first);
                    return true;
                }

            return false;
            }), buffers.end());
        }

        void clear_reservations(size_t tid)
        {
            reservations[tid].min_era.store(0, std::memory_order_relaxed);
            reservations[tid].max_era.store(0, std::memory_order_relaxed);
        }

        void clear(size_t tid)
        {
            thread[tid].clear();
            clear_reservations(tid);
        }
    };

    template< typename T, typename ThreadManager = thread, typename Allocator = std::allocator< T > > class hazard_era_allocator
    {
        template< typename U, typename ThreadManagerU, typename AllocatorU > friend class hazard_era_allocator;

        static_assert(std::is_empty_v< Allocator >);

        static const int freq = 1024;
        static_assert(freq % 2 == 0);

        using hazard_buffer_header = typename hazard_era_allocator_base< ThreadManager >::hazard_buffer_header;

        struct hazard_buffer
        {
            template< typename... Args > hazard_buffer(uint64_t era, Args&&... args)
                : header{ era, static_cast<uint64_t>(-1) }
                , value{ std::forward< Args >(args)... }
            {}

            hazard_buffer_header header;
            T value;
        };

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< hazard_buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

    public:
        template< typename U > struct rebind
        {
            using other = hazard_era_allocator< U, ThreadManager, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };
        
        hazard_era_allocator() {}
        template< typename U, typename AllocatorT > hazard_era_allocator(hazard_era_allocator< U, ThreadManager, AllocatorT >&) {}

        auto guard() { return base().guard(); }
        auto thread_id() { return base().thread_id(); }

        template< typename... Args > T* allocate(Args&&... args)
        {
            allocator_type allocator;
            auto buffer = allocator_traits_type::allocate(allocator, 1);
            allocator_traits_type::construct(allocator, buffer,
                base().era.load(std::memory_order_acquire), std::forward< Args >(args)...);

            if ((base().thread[thread_id()].allocated++ & (freq - 1)) == 0)
                base().era.fetch_add(1, std::memory_order_release);

            return &buffer->value;
        }

        T* protect(const std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst)
        {
            auto tid = thread_id();
            uint64_t max_era = base().reservations[tid].max_era.load(std::memory_order_relaxed);
            while (true)
            {
                auto* ret = value.load(order);
                uint64_t era = base().era.load(std::memory_order_acquire);
                if (max_era == era) return ret;
                if (max_era == 0)
                    base().reservations[tid].min_era.store(era, std::memory_order_release);
                base().reservations[tid].max_era.store(era, std::memory_order_release);
                max_era = era;
            }
        }

        void retire(T* ptr)
        {
            auto tid = thread_id();
            auto buffer = hazard_buffer_cast(ptr);
            buffer->header.retired = base().era.load(std::memory_order_acquire);
            base().thread[tid].retired_buffers.emplace_back(&buffer->header, &hazard_buffer_retire);

            if ((base().thread[tid].retired++ & (freq - 1)) == 0)
            {
                base().era.fetch_add(1, std::memory_order_release);
                base().cleanup();
            }
        }

        void deallocate(T* ptr)
        {
            hazard_buffer_deallocate(hazard_buffer_cast(ptr));
        }

    private:
        hazard_era_allocator_base< ThreadManager >& base()
        {
            return hazard_era_allocator_base< ThreadManager >::instance();
        }
        
        static void hazard_buffer_retire(hazard_buffer_header* ptr)
        {
            hazard_buffer_deallocate(hazard_buffer_cast(ptr));
        }

        static void hazard_buffer_deallocate(hazard_buffer* buffer)
        {
            allocator_type allocator;
            allocator_traits_type::destroy(allocator, buffer);
            allocator_traits_type::deallocate(allocator, buffer, 1);
        }

        static hazard_buffer* hazard_buffer_cast(T* ptr)
        {
            return reinterpret_cast< hazard_buffer* >(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, value));
        }

        static hazard_buffer* hazard_buffer_cast(hazard_buffer_header* ptr)
        {
            return reinterpret_cast< hazard_buffer* >(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, header));
        }
    };
}
