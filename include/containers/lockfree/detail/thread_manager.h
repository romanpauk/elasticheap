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
    template< size_t N = 1024 > class thread_manager
    {
        struct thread_registration
        {
            thread_registration(std::array< detail::aligned< std::atomic< uint64_t > >, N >& threads)
                : threads_(threads)
            {
                for (size_t i = 0; i < threads_.size(); ++i)
                {
                    auto tid = threads_[i].load(std::memory_order_relaxed);
                    if (!tid)
                    {
                        if (threads_[i].compare_exchange_strong(tid, gettid()))
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
                threads_[index_].store(0);
            }

            size_t index_;
            std::array< detail::aligned< std::atomic< uint64_t > >, N >& threads_;
        };

    public:
        static const int max_threads = N;

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

    private:
        alignas(64) std::array < detail::aligned< std::atomic< uint64_t > >, N > thread_ids;
    };

    using thread = thread_manager<>;
}
