//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <containers/lockfree/detail/exponential_backoff.h>
#include <containers/lockfree/detail/optional.h>
#include <containers/lockfree/atomic.h>

#include <atomic>
#include <memory>
#include <optional>
#include <cassert>

namespace containers
{
    //
    // BBQ: A Block-based Bounded Queue
    // https://www.usenix.org/conference/atc22/presentation/wang-jiawei
    //

    constexpr size_t log2(size_t value) { return value < 2 ? 1 : 1 + log2(value / 2); }

    template<
        typename T,
        size_t Size
    > class bounded_queue_bbq_base
    {
    public:
        using element_type = typename detail::optional< T >;

        static_assert(Size % 2 == 0);
        static_assert(std::is_nothrow_destructible_v< T >);
        
        enum class status
        {
            success,
            fail,
            busy,
            block_done,
        };

        struct cursor
        {
            cursor() = default;

            cursor(uint32_t off, uint32_t ver)
                : version(ver)
                , offset(off)
            {}

            cursor(uint64_t value)
                : version(value >> 32)
                , offset(value)
            {}

            operator uint64_t() { return (uint64_t)version << 32 | offset; }

            uint32_t offset;
            uint32_t version;
        };

        struct block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;
            alignas(64) std::array< element_type, Size > entries;
        };

        struct fetch_add
        {
            fetch_add(std::atomic< uint64_t >& counter) : counter_(counter) {}
            ~fetch_add() { counter_.fetch_add(1); }
        private:
            std::atomic< uint64_t >& counter_;
        };

        struct reset
        {
            reset(element_type& value) : value_(value) {}
            ~reset() { value_.reset(); }
        private:
            element_type& value_;
        };

        void initialize_block(block& block, uint64_t value)
        {
            block.allocated.store(value);
            block.committed.store(value);
            block.reserved.store(value);
            block.consumed.store(value);
        }

        static constexpr size_t capacity() { return Size; }

        detail::optional< T >& thread_local_optional()
        {
            static thread_local detail::optional< T > value;
            return value;
        }

        template< typename... Args > constexpr static bool is_thread_local_nothrow_constructible()
        {
            if constexpr (!std::is_nothrow_constructible_v< T, Args... >)
            {
                // Thread-local construction means the construction will be attempted outside the
                // queue first and successfully constructed object will be moved to the queue.
                // This way, queue metadata will always be right.
                static_assert(std::is_nothrow_move_constructible_v< T >);
                return true;
            }
            
            return false;
        }
    };
    
    // Single-use BBQ block that can be filled / depleted only once. The real queue
    // is in bounded_queue_bbq.h.
    template<
        typename T,
        size_t Size,
        typename Backoff = detail::exponential_backoff<>
    > class bounded_queue_bbq_block
        : bounded_queue_bbq_base< T, Size >
    {
        using base_type = bounded_queue_bbq_base< T, Size >;
        using cursor_type = typename base_type::cursor;
        using status_type = typename base_type::status;
        using block_type = typename base_type::block;
        using fetch_add_type = typename base_type::fetch_add;
        using reset_type = typename base_type::reset;

        struct entry
        {
            uint32_t offset;
            uint32_t version;
        };
        
        block_type block_;

        template< typename... Args > std::pair< status_type, entry > allocate_entry(Args&&... args)
        {
            if (cursor_type(block_.allocated.load()).offset >= Size)
                return { status_type::block_done, {} };

            if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
                this->thread_local_optional().emplace(std::forward< Args >(args)...);

            auto allocated = cursor_type(block_.allocated.fetch_add(1));
            if (allocated.offset >= Size)
            {
                if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
                    this->thread_local_optional().reset();

                return { status_type::block_done, {} };
            }
                
            return { status_type::success, { allocated.offset, 0 } };
        }

        template< typename... Args > void commit_entry(entry entry, Args&&... args)
        {
            fetch_add_type add(block_.committed);
            if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
            {
                block_.entries[entry.offset].emplace(std::move(thread_local_optional().value()));
                this->thread_local_optional().reset();
            }
            else
            {
                block_.entries[entry.offset].emplace(std::forward< Args >(args)...);
            }
        }

        std::pair< status_type, entry > reserve_entry(Backoff& backoff)
        {
            while (true)
            {
                auto reserved = cursor_type(block_.reserved.load());
                if (reserved.offset < Size)
                {
                    auto committed = cursor_type(block_.committed.load());
                    if (committed.offset == reserved.offset)
                        return { status_type::fail, {} };

                    if (committed.offset != Size)
                    {
                        auto allocated = cursor_type(block_.allocated.load());
                        if (committed.offset != allocated.offset)
                            return { status_type::busy, {} };
                    }

                    if (atomic_fetch_max_explicit(&block_.reserved, (uint64_t)cursor_type(reserved.offset + 1, reserved.version)) == (uint64_t)reserved)
                        return { status_type::success, { reserved.offset, reserved.version } };
                    else
                    {
                        backoff();
                        continue;
                    }
                }

                return { status_type::block_done, {} };
            }
        }

        void consume_entry(entry entry, std::optional< T >& value)
        {
            fetch_add_type add(block_.consumed);
            reset_type reset(block_.entries[entry.offset]);
            value.emplace(std::move(block_.entries[entry.offset].value()));
        }

        void consume_entry(entry entry, T& value)
        {
            fetch_add_type add(block_.consumed);
            reset_type reset(block_.entries[entry.offset]);
            value = std::move(block_.entries[entry.offset].value());
        }

    public:
        using value_type = T;

        bounded_queue_bbq_block()
        {
            this->initialize_block(block_, 0);
        }

        ~bounded_queue_bbq_block()
        {
            for (size_t i = cursor_type(block_.consumed).offset; i < cursor_type(block_.committed).offset; ++i)
            {
                block_.entries[i].reset();
            }
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto [status, entry] = allocate_entry(std::forward< Args >(args)...);
                switch (status)
                {
                case status_type::success:
                    commit_entry(entry, std::forward< Args >(args)...);
                    return true;
                case status_type::block_done:
                case status_type::fail: return false;
                case status_type::busy: break;
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
                case status_type::success:
                    consume_entry(entry, result);
                    return true;
                case status_type::fail: return false;
                case status_type::busy: break;
                case status_type::block_done: return false;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        bool empty() const
        {
            auto reserved = cursor_type(block_.reserved.load());
            if (reserved.offset < Size)
            {
                auto committed = cursor_type(block_.committed.load());
                if (committed.offset == reserved.offset)
                    return true;
            }

            return false;
        }
    };

    template<
        typename T,
        size_t Size,
        size_t BlockSize = Size / (1 << (std::max(size_t(1), log2(Size) / 4) - 1)), // log(num of blocks) = max(1, log(size)/4)
        typename Backoff = detail::exponential_backoff<>
    > class bounded_queue_bbq
        : bounded_queue_bbq_base< T, Size >
    {
        static_assert(BlockSize % 2 == 0);
        static_assert(Size / BlockSize > 1);

        using base_type = bounded_queue_bbq_base< T, Size >;
        using cursor_type = typename base_type::cursor;
        using status_type = typename base_type::status;
        using element_type = typename base_type::element_type;
        using block_type = typename base_type::block;
        using fetch_add_type = typename base_type::fetch_add;
        using reset_type = typename base_type::reset;

        // TODO: spsc mode
        // TODO: drop mode

        struct entry
        {
            block_type* block;
            uint32_t offset;
            uint32_t version;
        };
        
        // mutable is needed to support empty() as that can require moving to next block
        alignas(64) mutable std::array< block_type, Size / BlockSize > blocks_;
        alignas(64) std::atomic< uint64_t > phead_{};
        alignas(64) mutable std::atomic< uint64_t > chead_{};

        std::pair< cursor_type, block_type* > get_block(std::atomic< uint64_t >& head) const
        {
            auto value = cursor_type(head.load());
            return { value, &blocks_[value.offset & (blocks_.size() - 1)] };
        }

        template< typename... Args > std::pair< status_type, entry > allocate_entry(block_type* block, Args&&... args)
        {
            if (cursor_type(block->allocated.load()).offset >= BlockSize)
                return { status_type::block_done, {} };

            if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
                this->thread_local_optional().emplace(std::forward< Args >(args)...);
                
            auto allocated = cursor_type(block->allocated.fetch_add(1));
            if (allocated.offset >= BlockSize)
            {
                if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
                    this->thread_local_optional().reset();
                
                return { status_type::block_done, {} };
            }
            return { status_type::success, { block, allocated.offset, 0 } };
        }

        template< typename... Args > void commit_entry(entry entry, Args&&... args)
        {
            fetch_add_type add(entry.block->committed);
            if constexpr (this->is_thread_local_nothrow_constructible< Args... >())
            {
                entry.block->entries[entry.offset].emplace(std::move(this->thread_local_optional().value()));
                this->thread_local_optional().reset();
            } 
            else
            {
                entry.block->entries[entry.offset].emplace(std::forward< Args >(args)...);
            }
        }

        std::pair< status_type, entry > reserve_entry(block_type* block, Backoff& backoff)
        {
            while (true)
            {
                auto reserved = cursor_type(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = cursor_type(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return { status_type::fail, {} };

                    if (committed.offset != BlockSize)
                    {
                        auto allocated = cursor_type(block->allocated.load());
                        if (committed.offset != allocated.offset)
                            return { status_type::busy, {} };
                    }

                    if (atomic_fetch_max_explicit(&block->reserved, (uint64_t)cursor_type(reserved.offset + 1, reserved.version)) == (uint64_t)reserved)
                        return { status_type::success, { block, reserved.offset, reserved.version } };
                    else
                    {
                        backoff();
                        continue;
                    }
                }

                return { status_type::block_done, {} };
            }
        }

        void consume_entry(entry entry, std::optional< T >& value)
        {
            fetch_add_type add(entry.block->consumed);
            reset_type reset(entry.block->entries[entry.offset]);
            value.emplace(std::move(entry.block->entries[entry.offset].value()));
            
            // Drop-old mode:
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) value.reset();
            //return data;
        }

        void consume_entry(entry entry, T& value)
        {
            fetch_add_type add(entry.block->consumed);
            reset_type reset(entry.block->entries[entry.offset]);
            value = std::move(entry.block->entries[entry.offset].value());
            
            // Drop-old mode:
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) value.reset();
        }

        status_type advance_phead(cursor_type head)
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto consumed = cursor_type(next_block.consumed.load());
            if (consumed.version < head.version ||
                (consumed.version == head.version && consumed.offset != BlockSize))
            {
                auto reserved = cursor_type(next_block.reserved.load());
                if (reserved.offset == consumed.offset)
                    return status_type::fail;
                else
                    return status_type::busy;
            }
            // Drop-old mode:
            //auto committed = Cursor(next_block.committed.load());
            //if (commited.version == head.version && commited.index != BlockSize)
            //    return advance_status_type::not_available;
            atomic_fetch_max_explicit(&next_block.committed, (uint64_t)cursor_type(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.allocated, (uint64_t)cursor_type(0, head.version + 1));

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&phead_, (uint64_t)cursor_type(head.offset + 1, head.version));
            return status_type::success;
        }

        bool advance_chead(cursor_type head, uint32_t version) const
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto committed = cursor_type(next_block.committed.load());
            if (committed.version != head.version + 1)
                return false;
            atomic_fetch_max_explicit(&next_block.consumed, (uint64_t)cursor_type(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.reserved, (uint64_t)cursor_type(0, head.version + 1));
            // Drop-old mode:
            //if (committed.version < version + (head.index == 0))
            //    return false;
            //atomic_fetch_and_max(next_block.reserved, { 0, committed.version });

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&chead_, (uint64_t)cursor_type(head.offset + 1, head.version));
            return true;
        }

    public:
        using value_type = T;

        bounded_queue_bbq()
        {
            this->initialize_block(blocks_[0], 0);
            for (size_t i = 1; i < blocks_.size(); ++i)
                this->initialize_block(blocks_[i], BlockSize);
        }

        ~bounded_queue_bbq()
        {
            // TODO: iterate only blocks that are unconsumed
            for (auto& block: blocks_)
            {
                for (size_t i = cursor_type(block.consumed).offset; i < cursor_type(block.committed).offset; ++i)
                {
                    block.entries[i].reset();
                }
            }
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(phead_);
                auto [status, entry] = allocate_entry(block, std::forward< Args >(args)...);
                switch (status)
                {
                case status_type::success:
                    commit_entry(entry, std::forward< Args >(args)...);
                    return true;
                case status_type::block_done:
                    switch (advance_phead(head))
                    {
                    case status_type::success: continue;
                    case status_type::fail: return false;
                    case status_type::busy: break;
                    default: assert(false);
                    }
                default:
                    assert(false);
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
                auto [head, block] = get_block(chead_);
                auto [status, entry] = reserve_entry(block, backoff);
                switch (status)
                {
                case status_type::success:
                    consume_entry(entry, result);
                    return true;
                case status_type::fail: return false;
                case status_type::busy: break;
                case status_type::block_done:
                    if (!advance_chead(head, entry.version))
                        return false;
                    continue;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        bool empty() const
        {
            while (true)
            {
                auto [head, block] = get_block(chead_);
                auto reserved = cursor_type(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = cursor_type(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return true;

                    return false;
                }
                else if (!advance_chead(head, reserved.version))
                {
                    return false;
                }
            }
        }
    };
}
