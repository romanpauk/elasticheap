//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
static size_t gettid() { return GetCurrentThreadId(); }
#else
#include <unistd.h>
#include <sys/types.h>
#endif

namespace containers::detail
{
    template< size_t N = 32 > class thread_manager
    {
    public:
        static const int max_threads = N;
        
        static size_t id()
        {
            return id_;
        }
    /*
        static size_t token()
        {
        #if defined(_WIN32)
            return mix(__readgsqword(0x30)); // Address of Thread Information Block
        #else
            return mix(reinterpret_cast<size_t>(&token_));
        #endif
        }
    */  
    private:
        static size_t mix(size_t value)
        {
            // Bring upper bits down so they impact 'value & (size - 1)'
            return value ^= (value >> 47) ^ (value >> 23);
        }

        static size_t register_thread()
        {
            struct thread_registration
            {
                thread_registration()
                {
                    for (size_t i = 0; i < thread_ids_.size(); ++i)
                    {
                        auto tid = thread_ids_[i].load(std::memory_order_relaxed);
                        if (!tid)
                        {
                            if (thread_ids_[i].compare_exchange_strong(tid, gettid()))
                            {
                                index_ = i;
                                return;
                            }
                        }
                        else if (tid == gettid())
                        {
                            // Check for multiple thread registrations of the same thread. Failure here
                            // means the singleton is placed somewhere where it should not be, so it gets initialized
                            // multiple times.
                            std::abort();
                        }
                    }

                    // Failed to find free spot for a thread
                    std::abort();
                }

                ~thread_registration()
                {
                    thread_ids_[index_].store(0);
                }

                size_t index_;
            };

            static thread_local thread_registration registration;
            return registration.index_;
        }

        alignas(64) static std::array < std::atomic< uint64_t >, N > thread_ids_;
        alignas(64) static thread_local size_t id_;
    };

    template< size_t N > alignas(64) std::array < std::atomic< uint64_t >, N > thread_manager< N >::thread_ids_;
    template< size_t N > alignas(64) thread_local size_t thread_manager< N >::id_ = thread_manager< N >::register_thread();

    using thread = thread_manager<>;
}
