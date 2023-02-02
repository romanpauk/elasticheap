//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <optional>
#include <atomic>

#include <containers/lockfree/atomic.h>

namespace containers
{
    // Single-use BBQ block that can be filled / depleted only once. The real queue
    // is in bounded_queue_bbq.h.
    template<
        typename T,
        size_t Size,
        typename Backoff
    > class bounded_queue_bbq_block
    {
        enum class status
        {
            success,
            fail,
            busy,
            block_done,
        };

        struct Cursor
        {
            Cursor() = default;

            Cursor(uint32_t off, uint32_t ver)
                : version(ver)
                , offset(off)
            {}

            Cursor(uint64_t value)
                : version(value >> 32)
                , offset(value)
            {}

            operator uint64_t() { return (uint64_t)version << 32 | offset; }

            uint32_t offset;
            uint32_t version;
        };

        struct Block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;
            alignas(64) std::array< detail::optional< T >, Size > entries;
        };

        struct Entry
        {
            uint32_t offset;
            uint32_t version;
        };

        static_assert(Size % 2 == 0);

        Block block_;

        std::pair< status, Entry > allocate_entry()
        {
            if (Cursor(block_.allocated.load()).offset >= Size)
                return { status::block_done, {} };
            auto allocated = Cursor(block_.allocated.fetch_add(1));
            if (allocated.offset >= Size)
                return { status::block_done, {} };
            return { status::success, { allocated.offset, 0 } };
        }

        template< typename... Args > void commit_entry(Entry entry, Args&&... args)
        {
            block_.entries[entry.offset].emplace(std::forward< Args >(args)...);
            block_.committed.fetch_add(1);
        }

        std::pair< status, Entry > reserve_entry(Backoff& backoff)
        {
            while (true)
            {
                auto reserved = Cursor(block_.reserved.load());
                if (reserved.offset < Size)
                {
                    auto committed = Cursor(block_.committed.load());
                    if (committed.offset == reserved.offset)
                        return { status::fail, {} };

                    if (committed.offset != Size)
                    {
                        auto allocated = Cursor(block_.allocated.load());
                        if (committed.offset != allocated.offset)
                            return { status::busy, {} };
                    }

                    if (atomic_fetch_max_explicit(&block_.reserved, (uint64_t)Cursor(reserved.offset + 1, reserved.version)) == (uint64_t)reserved)
                        return { status::success, { reserved.offset, reserved.version } };
                    else
                    {
                        backoff();
                        continue;
                    }
                }

                return { status::block_done, {} };
            }
        }

        void consume_entry(Entry entry, std::optional< T >& value)
        {
            value.emplace(std::move(block_.entries[entry.offset].value()));
            block_.entries[entry.offset].reset();
            block_.consumed.fetch_add(1);
        }

        void consume_entry(Entry entry, T& value)
        {
            value = std::move(block_.entries[entry.offset].value());
            block_.entries[entry.offset].reset();
            block_.consumed.fetch_add(1);
        }

    public:
        using value_type = T;

        bounded_queue_bbq_block()
        {
            block_.allocated.store(0);
            block_.committed.store(0);
            block_.reserved.store(0);
            block_.consumed.store(0);
        }

        ~bounded_queue_bbq_block()
        {
            for (size_t i = Cursor(block_.consumed).offset; i < Cursor(block_.committed).offset; ++i)
            {
                block_.entries[i].reset();
            }
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto [status, entry] = allocate_entry();
                switch (status)
                {
                case status::success:
                    commit_entry(entry, std::forward< Args >(args)...);
                    return true;
                case status::block_done:
                case status::fail: return false;
                case status::busy: break;
                default: assert(false);
                }

                backoff();
            }
        }

        bool push(const T& value) { return emplace(value); }
        bool push(T&& value) { return emplace(std::move(value)); }

        template< typename Result > bool pop(Result& result)
        {
            Backoff backoff;
            while (true)
            {
                auto [status, entry] = reserve_entry(backoff);
                switch (status)
                {
                case status::success:
                    consume_entry(entry, result);
                    return true;
                case status::fail: return false;
                case status::busy: break;
                case status::block_done: return false;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        static constexpr size_t capacity() { return Size; }

        bool empty() const
        {
            auto reserved = Cursor(block_.reserved.load());
            if (reserved.offset < Size)
            {
                auto committed = Cursor(block_.committed.load());
                if (committed.offset == reserved.offset)
                    return true;
            }

            return false;
        }
    };
}
