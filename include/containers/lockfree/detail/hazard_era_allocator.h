//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

// Hazard Eras - Non-Blocking Memory Reclamation - https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf
// Universal Wait-Free Memory Reclamation - https://arxiv.org/abs/2001.01999

#include <containers/lockfree/detail/aligned.h>

#include <cassert>
#include <array>
#include <vector>
#include <atomic>

#define NOMINMAX
#include <windows.h>

namespace containers
{
    template< size_t N > struct is_power_of_2 { static constexpr bool value = (N & (N - 1)) == 0; };

    template< size_t Initial = 256, size_t Max = 65536 > struct exp_backoff
    {
        static_assert(is_power_of_2< Initial >::value);
        static_assert(is_power_of_2< Max >::value);

        void operator() ()
        {
            size_t iters = state_;
            if((state_ <<= 1) >= Max)
                state_ = Max;

            while (iters--)
                _mm_pause();
        }

    private:
        size_t state_ = Initial;
    };

    template< typename ThreadManager > struct thread_guard
    {
        ~thread_guard() { manager->clear(); }
        ThreadManager* manager;
    };

    template< size_t N = 128 > class thread_manager
    {
        friend class thread_guard< thread_manager< N > >;

        struct thread_registration
        {
            thread_registration(std::array< aligned< std::atomic< uint64_t > >, N >& threads)
                : threads_(threads)
            {                
                for (size_t i = 0; i < threads_.size(); ++i)
                {
                    auto tid = threads_[i].load(std::memory_order_relaxed);
                    if (!tid)
                    {
                        if (threads_[i].compare_exchange_strong(tid, GetCurrentThreadId()))
                        {
                            index_ = i;
                            return;
                        }
                    }
                    else if (tid == GetCurrentThreadId())
                    {
                        // Check for multiple thread registrations of the same thread
                        break;
                    }
                }

                std::abort();
            }

            ~thread_registration()
            {
                threads_[index_].store(0);
            }
        
            size_t index_;
            std::array< aligned< std::atomic< uint64_t > >, N >& threads_;
        };

    public:
        static const int max_threads = N;

        thread_guard< thread_manager< N > > guard() { return { this }; }

        static thread_manager< N >& instance()
        {
            static thread_manager< N > instance;
            return instance;
        }

        size_t id()
        {
            static thread_local thread_registration registration(thread_ids);
            return registration.index_;
        }

    //protected:
        void clear()
        {
            reservations[id()].min_era.store(0, std::memory_order_relaxed);
            reservations[id()].max_era.store(0, std::memory_order_relaxed);
        }

        struct thread_reservation
        {
            std::atomic< uint64_t > min_era;
            std::atomic< uint64_t > max_era;
        };       

        alignas(64) std::atomic< uint64_t > era = 1;
        alignas(64) std::array< aligned< thread_reservation >, N > reservations;
        alignas(64) std::array < aligned< std::atomic< uint64_t > >, N > thread_ids;
    };
    
    using thread = thread_manager<>;

    // - Thread ADT state - shared between all ADT<T>'s of concrete T
    //      allocator<T, ThreadManager> allocator(threads);
    //          thread-local data for T
    //
    //  - ADT state - state of concrete ADT
    //      queue< T, Allocator > queue(allocator);

    template< typename T, typename ThreadManager = thread, typename Allocator = std::allocator< T > > class hazard_era_allocator
    {
        static const int freq = 1024;
        static_assert(is_power_of_2< freq >::value);

        struct hazard_buffer
        {
            template< typename... Args > hazard_buffer(uint64_t era, Args&&... args)
                : value{std::forward< Args >(args)...}
                , allocated(era)
                , retired(-1)
            {}

            uint64_t allocated;
            uint64_t retired;
            T value;
        };

        struct thread_data
        {
            uint64_t allocated;
            uint64_t retired;

            // TODO: retire list needs to be passed to someone upon thread exit
            std::vector< hazard_buffer* > retired_buffers;
            
            void clear()
            {
                allocated = retired = 0;
            }
        };

        alignas(64) std::array< aligned< thread_data >, ThreadManager::max_threads > thread_;

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< hazard_buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;
        ThreadManager& thread_manager_;

    public:
        template< typename U > struct rebind
        {
            using other = hazard_era_allocator< U, ThreadManager, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };
        
        hazard_era_allocator(ThreadManager& thread_manager = ThreadManager::instance())
            : thread_manager_(thread_manager)
        {}

        auto guard() { return thread_manager_.guard(); }

        static hazard_era_allocator< T, ThreadManager, Allocator >& instance()
        {
            static hazard_era_allocator< T, ThreadManager, Allocator > instance;
            return instance;
        }

        template< typename... Args > T* allocate(Args&&... args)
        {
            auto buffer = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, buffer,
                thread_manager_.era.load(std::memory_order_acquire), std::forward< Args >(args)...);

            if (thread_[thread_id()].allocated++ % freq == 0)
                thread_manager_.era.fetch_add(1, std::memory_order_release);

            return &buffer->value;
        }

        T* protect(std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst)
        {
            uint64_t max_era = thread_manager_.reservations[thread_id()].max_era.load(std::memory_order_relaxed);
            while (true)
            {
                auto* ret = value.load(order);
                uint64_t era = thread_manager_.era.load(std::memory_order_acquire);
                if (max_era == era) return ret;
                if (max_era == 0)
                    thread_manager_.reservations[thread_id()].min_era.store(era, std::memory_order_release);
                thread_manager_.reservations[thread_id()].max_era.store(era, std::memory_order_release);
                max_era = era;
            }
        }

        T* protect(T* value, std::memory_order order = std::memory_order_seq_cst)
        {
            uint64_t max_era = thread_manager_.reservations[thread_id()].max_era.load(std::memory_order_relaxed);
            while (true)
            {
                uint64_t era = thread_manager_.era.load(std::memory_order_acquire);
                if (max_era == era) return value;
                if (max_era == 0)
                    thread_manager_.reservations[thread_id()].min_era.store(era, std::memory_order_release);
                thread_manager_.reservations[thread_id()].max_era.store(era, std::memory_order_release);
                max_era = era;
            }
        }

        void retire(T* ptr)
        {
            auto buffer = hazard_buffer_cast(ptr);
            buffer->retired = thread_manager_.era.load(std::memory_order_relaxed);
            thread_[thread_id()].retired_buffers.push_back(buffer);

            if (thread_[thread_id()].retired++ % freq == 0)
            {
                thread_manager_.era.fetch_add(1, std::memory_order_release);
                cleanup();
            }
        }

        void deallocate_unsafe(T* ptr)
        {
            auto buffer = hazard_buffer_cast(ptr);
            allocator_traits_type::destroy(allocator_, buffer);
            allocator_traits_type::deallocate(allocator_, buffer, 1);
        }

        size_t thread_id()
        {
            // Register thread first, dtor second, so dtor runs before thread gets unregistered
            // TODO: not sure how valid 'this' will be when last thread will get destroyed...

            static thread_local size_t id = thread_manager_.id();
            static thread_local struct thread_destructor
            {
                thread_destructor(hazard_era_allocator< T, ThreadManager, Allocator >& allocator)
                    : allocator_(allocator)
                {}

                ~thread_destructor()
                {
                    // Clean thread's thread-local data
                    allocator_.thread_[allocator_.thread_manager_.id()].clear();

                    // Clean threads's era reservations
                    allocator_.thread_manager_.clear();
                }

                hazard_era_allocator< T, ThreadManager, Allocator >& allocator_;
            } dtor(*this);
            return id;
        }

    private:
        void cleanup()
        {
            auto& buffers = thread_[thread_id()].retired_buffers;
            buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [this](hazard_buffer* buffer)
            {
                if (can_deallocate(buffer))
                {
                    allocator_traits_type::destroy(allocator_, buffer);
                    allocator_traits_type::deallocate(allocator_, buffer, 1);
                    return true;
                }

                return false;
            }), buffers.end());
        }

        bool can_deallocate(hazard_buffer* buffer)
        {
            for (size_t tid = 0; tid < ThreadManager::max_threads; ++tid)
            {
                auto min_era = thread_manager_.reservations[tid].min_era.load(std::memory_order_acquire);
                auto max_era = thread_manager_.reservations[tid].max_era.load(std::memory_order_acquire);

                if (min_era <= buffer->allocated && buffer->allocated <= max_era)
                    return false;
                if (min_era <= buffer->retired && buffer->retired <= max_era)
                    return false;
                if (buffer->allocated <= min_era && buffer->retired >= max_era)
                    return false;
            }

            return true;
        }

        hazard_buffer* hazard_buffer_cast(T* ptr)
        {
            return reinterpret_cast<hazard_buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, value));
        }
    };
}
