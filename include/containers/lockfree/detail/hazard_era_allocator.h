//
// This file is part of smart_ptr project <https://github.com/romanpauk/smart_ptr>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

// Hazard Eras - Non-Blocking Memory Reclamation - https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/hazarderas-2017.pdf
// Universal Wait-Free Memory Reclamation - https://arxiv.org/abs/2001.01999

#include <cassert>
#include <array>
#include <vector>
#include <atomic>

namespace containers
{
    template< typename T > struct thread_registration
    {
        static const size_t max_threads = 32;

        thread_registration()
        {
            for (size_t i = 0; i < threads_.size(); ++i)
            {
                auto id = threads_[i].load(std::memory_order_relaxed);
                if (!id)
                {
                    if (threads_[i].compare_exchange_strong(id, &thread_id_))
                    {
                        thread_id_ = i;
                        return;
                    }
                }
            }

            std::abort();
        }

        ~thread_registration()
        {
            threads_[thread_id_].store(0);
        }

        static size_t id() { return thread_id_; }

    private:
        static thread_local size_t thread_id_;
        static std::array< std::atomic< void* >, max_threads > threads_;
    };

    template< typename T > thread_local size_t thread_registration< T >::thread_id_;
    template< typename T > std::array< std::atomic< void* >, thread_registration< T >::max_threads > thread_registration< T >::threads_;

    using thread = thread_registration< void >;   
        
    // TODO: each hazard era thread needs to clean up its reservations before exit
    // It also needs to pass its retire_list to someone, making retire lists more than vectors

    template< typename T, size_t N, typename Allocator = std::allocator< T > > class hazard_era_allocator
    {
        static const int freq = 128;

        struct hazard_buffer
        {
            template< typename... Args > hazard_buffer(uint64_t era, Args&&... args)
                : value(std::forward< Args >(args)...)
                , allocate_era(era)
                , retire_era(-1)
            {}

            uint64_t allocate_era;
            uint64_t retire_era;
            T value;
        };

        struct local_data
        {
            uint64_t allocated;
            uint64_t retired;
            std::vector< hazard_buffer* > retired_buffers;
        };

        struct global_data
        {
            alignas(64) std::atomic< uint64_t > era;
            alignas(64) std::atomic< uint64_t > reservations[thread::max_threads][N];
            //std::array< std::atomic< void* >, thread::max_threads > threads;
        };

        static global_data global_;
        static thread_local local_data local_;
        static thread_local thread thread_;

        using allocator_type = std::allocator_traits< Allocator >::template rebind_alloc< hazard_buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;

    public:
        template< typename... Args > T* allocate(Args&&... args)
        {
            if (local_.allocated++ % freq == 0)
                global_.era.fetch_add(1, std::memory_order_release);

            auto buffer = allocator_traits_type::allocate(allocator_, 1);
            allocator_traits_type::construct(allocator_, buffer,
                global_.era.load(std::memory_order_acquire), std::forward< Args >(args)...);
            return &buffer->value;
        }

        template< size_t Index > T* protect(std::atomic< T* >& val, std::memory_order order = std::memory_order_seq_cst)
        {
            static_assert(Index < N);
            uint64_t old_era = global_.reservations[thread::id()][Index].load(std::memory_order_relaxed);
            while (true)
            {
                auto* ret = val.load(order);
                uint64_t new_era = global_.era.load(std::memory_order_acquire);
                if (old_era == new_era) return ret;
                global_.reservations[thread::id()][Index].store(new_era, std::memory_order_release);
                old_era = new_era;
            }
        }

        void deallocate(T* ptr)
        {
            auto buffer = hazard_buffer_cast(ptr);
            buffer->retire_era = global_.era.load(std::memory_order_acquire);
            local_.retired_buffers.push_back(buffer);
            if (local_.retired++ % freq == 0) {
                global_.era.fetch_add(1, std::memory_order_release);
                cleanup();
            }
        }

        void deallocate_unsafe(T* ptr)
        {
            auto buffer = hazard_buffer_cast(ptr);
            allocator_traits_type::destroy(allocator_, buffer);
            allocator_traits_type::deallocate(allocator_, buffer, 1);
        }

        void clear()
        {
            auto tid = thread::id();
            for (size_t i = 0; i < N; ++i)
                global_.reservations[tid][i].store(0, std::memory_order_relaxed);
        }

    private:
        void cleanup()
        {
            auto& buffers = local_.retired_buffers;
            buffers.erase(std::remove_if(buffers.begin(), buffers.end(), [this](hazard_buffer* buffer)
            {
                if (can_delete(buffer))
                {
                    allocator_traits_type::destroy(allocator_, buffer);
                    allocator_traits_type::deallocate(allocator_, buffer, 1);
                    return true;
                }

                return false;
            }), buffers.end());
        }

        bool can_delete(hazard_buffer* buffer)
        {
            for (size_t tid = 0; tid < thread::max_threads; ++tid)
            {
                for (size_t i = 0; i < N; ++i)
                {
                    uint64_t era = global_.reservations[tid][i].load(std::memory_order_acquire);
                    if (era != -1 && buffer->allocate_era <= era && buffer->retire_era >= era)
                        return false;
                }
            }

            return true;
        }

        hazard_buffer* hazard_buffer_cast(T* ptr)
        {
            return reinterpret_cast<hazard_buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(hazard_buffer, value));
        }
    };

    template< typename T, size_t N, typename Allocator > hazard_era_allocator< T, N, Allocator >::global_data hazard_era_allocator< T, N, Allocator >::global_;
    template< typename T, size_t N, typename Allocator > thread_local hazard_era_allocator< T, N, Allocator >::local_data hazard_era_allocator< T, N, Allocator >::local_;
    template< typename T, size_t N, typename Allocator > thread_local thread hazard_era_allocator< T, N, Allocator >::thread_;
}
