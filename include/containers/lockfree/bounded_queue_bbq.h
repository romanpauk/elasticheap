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

    struct entry
    {
        uint32_t offset;
        uint32_t version;
    };

    struct fetch_add
    {
        fetch_add(std::atomic< uint64_t >& counter) : counter_(counter) {}
        ~fetch_add() { counter_.fetch_add(1); }
    private:
        std::atomic< uint64_t >& counter_;
    };

    template<
        typename T,
        size_t Size,
        bool IsTrivial = detail::is_trivial_v< T >
    > class bounded_queue_bbq_base;

    template<
        typename T,
        size_t Size
    > class bounded_queue_bbq_base< T, Size, true >
    {
        using element = detail::optional< T, true >;

    public:
        struct block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;

            alignas(64) std::array< element, Size > entries;
        };

        static void initialize_block(block& block, uint64_t value)
        {
            block.allocated.store(value);
            block.committed.store(value);
            block.reserved.store(value);
            block.consumed.store(value);
        }

        template< typename Blocks > static void destroy_blocks(Blocks&) {}
        template< typename Block > static void destroy_block(Block&) {}

        template< typename... Args > static void commit_entry(block* block, const entry& entry, Args&&... args)
        {
            static_assert(std::is_nothrow_constructible_v< T, Args... >);

            block->entries[entry.offset].emplace(std::forward< Args >(args)...);
            block->committed.fetch_add(1);
        }

        static void consume_entry(block* block, const entry& entry, std::optional< T >& value)
        {
            static_assert(std::is_nothrow_move_assignable_v< T >);

            value = std::move(block->entries[entry.offset].value());
            block->consumed.fetch_add(1);

            // TODO: drop-old mode
        }

        static void consume_entry(block* block, const entry& entry, T& value)
        {
            static_assert(std::is_nothrow_move_assignable_v< T >);

            value = std::move(block->entries[entry.offset].value());
            block->consumed.fetch_add(1);

            // TODO: drop-old mode
        }

        static bool empty_check(const block*, cursor, cursor) { return false; }
    };

    template<
        typename T,
        size_t Size
    > class bounded_queue_bbq_base< T, Size, false >
    {
        using element = detail::optional< T, false >;

    public:        
        struct block
        {
            alignas(64) std::atomic< uint64_t > allocated;
            alignas(64) std::atomic< uint64_t > committed;
            alignas(64) std::atomic< uint64_t > reserved;
            alignas(64) std::atomic< uint64_t > consumed;

            alignas(64) std::array< bool, Size > flags;
            alignas(64) std::array< element, Size > entries;
        };
       
        struct reset
        {
            reset(element& value) : value_(value) {}
            ~reset() { value_.reset(); }
        private:
            element& value_;
        };

        static void initialize_block(block& block, uint64_t value)
        {
            block.allocated.store(value);
            block.committed.store(value);
            block.reserved.store(value);
            block.consumed.store(value);

            for (auto& flag : block.flags)
                flag = true;
        }

        template< typename Block > static void destroy_block(Block& block)
        {
            for (size_t i = cursor(block.consumed).offset; i < cursor(block.committed).offset; ++i)
            {
                block.entries[i].reset();
            }
        }

        template< typename Blocks > static void destroy_blocks(Blocks& blocks)
        {
            // TODO: iterate only blocks that are unconsumed
            for (auto& block : blocks)
                destroy_block(block);
        }

        template< typename U = T, typename... Args >
        static std::enable_if_t< std::is_nothrow_constructible_v< T, Args... >, void > commit_entry(block* block, const entry& entry, Args&&... args)
        {
            block->entries[entry.offset].emplace(std::forward< Args >(args)...);
            block->committed.fetch_add(1);
        }

        template< typename U = T, typename... Args >
        static std::enable_if_t< !std::is_nothrow_constructible_v< T, Args... >, void > commit_entry(block* block, const entry& entry, Args&&... args)
        {
            fetch_add add(block->committed);
            block->flags[entry.offset] = false;
            block->entries[entry.offset].emplace(std::forward< Args >(args)...);
            block->flags[entry.offset] = true;
        }
        
        static bool consume_entry(block* block, const entry& entry, std::optional< T >& value)
        {
            fetch_add add(block->consumed);
            if (block->flags[entry.offset])
            {
                reset reset(block->entries[entry.offset]);
                value.emplace(std::move(block->entries[entry.offset].value()));
                return true;
            }
            else
            {
                block->flags[entry.offset] = true;
                return false;
            }

            // TODO: drop-old mode
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) value.reset();
        }

        static bool consume_entry(block* block, const entry& entry, T& value)
        {
            fetch_add add(block->consumed);
            if (block->flags[entry.offset])
            {
                reset reset(block->entries[entry.offset]);
                value = std::move(block->entries[entry.offset].value());
                return true;
            }
            else
            {
                block->flags[entry.offset] = true;
                return false;
            }

            // TODO: drop-old mode
            //auto allocated = entry.block->allocated.load();
            //if(allocated.version != entry.version) value.reset();
        }

        static bool empty_check(const block* block, cursor reserved, cursor committed)
        {
            // Check that any elements are really there (they could be allocated
            // in the block, but not constructed due to exception)
            for (size_t index = reserved; index < committed; ++index)
                if (block->flags[index])
                    return false;

            return true;
        }
    };

    template<
        typename T,
        size_t Size,
        size_t BlockSize,
        typename Backoff
    > class bounded_queue_bbq_common
        : public bounded_queue_bbq_base< T, Size >
    {
        static_assert(Size % 2 == 0);
        static_assert(std::is_nothrow_destructible_v< T >);

        using base = bounded_queue_bbq_base< T, Size >;
        using block = typename base::block;

    public:
        static std::pair< status, entry > allocate_entry(block* block)
        {
            if (cursor(block->allocated.load()).offset >= BlockSize)
                return { status::block_done, {} };

            auto allocated = cursor(block->allocated.fetch_add(1));
            if (allocated.offset >= BlockSize)
            {
                return { status::block_done, {} };
            }
            return { status::success, { allocated.offset, 0 } };
        }

        static std::pair< status, entry > reserve_entry(block* block, Backoff& backoff)
        {
            while (true)
            {
                auto reserved = cursor(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = cursor(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return { status::fail, {} };

                    if (committed.offset != BlockSize)
                    {
                        auto allocated = cursor(block->allocated.load());
                        if (committed.offset != allocated.offset)
                            return { status::busy, {} };
                    }

                    if (atomic_fetch_max_explicit(&block->reserved, (uint64_t)cursor(reserved.offset + 1, reserved.version)) == (uint64_t)reserved)
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

        template< typename... Args > status emplace_block(block* block, Args&&... args)
        {
            auto [status, entry] = this->allocate_entry(block);
            if (status == status::success)
            {
                this->commit_entry(block, entry, std::forward< Args >(args)...);
            }
            return status;
        }

        static constexpr size_t capacity() { return Size; }
    };

    // Single-use BBQ block that can be filled / depleted only once.
    template<
        typename T,
        size_t Size,
        typename Backoff = detail::exponential_backoff<>
    > class bounded_queue_bbq_block
        : bounded_queue_bbq_common< T, Size, Size, Backoff >
    {
        using base = bounded_queue_bbq_base< T, Size >;
        using block = typename base::block;
                
        block block_;

    public:
        using value_type = T;

        bounded_queue_bbq_block()
        {
            this->initialize_block(block_, 0);
        }

        ~bounded_queue_bbq_block()
        {
            this->destroy_block(block_);
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto status = this->emplace_block(&block_, std::forward< Args >(args)...);
                switch (status)
                {
                case status::success:
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

        template< typename U = T, typename Result >
        std::enable_if_t< detail::is_trivial_v< U >, bool > pop(Result& result)
        {
            Backoff backoff;
            while (true)
            {
                auto [status, entry] = this->reserve_entry(&block_, backoff);
                switch (status)
                {
                case status::success:
                    this->consume_entry(&block_, entry, result);
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

        template< typename U = T, typename Result >
        std::enable_if_t< !detail::is_trivial_v< U >, bool > pop(Result& result)
        {
            Backoff backoff;
            while (true)
            {
                auto [status, entry] = this->reserve_entry(&block_, backoff);
                switch (status)
                {
                case status::success:
                    if(this->consume_entry(&block_, entry, result)) return true; else continue;
                case status::fail: return false;
                case status::busy: break;
                case status::block_done: return false;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        bool empty() const
        {
            auto reserved = cursor(block_.reserved.load());
            if (reserved.offset < Size)
            {
                auto committed = cursor(block_.committed.load());
                if (committed.offset == reserved.offset)
                    return true;

                return this->empty_check(&block_, reserved, committed);
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
        : bounded_queue_bbq_common< T, Size, BlockSize, Backoff >
    {
        using base = bounded_queue_bbq_base< T, Size >;
        using block = typename base::block;

        // TODO: spsc mode
        // TODO: drop mode

        // mutable is needed to support empty() as that can require moving to next block
        alignas(64) mutable std::array< block, Size / BlockSize > blocks_;
        alignas(64) std::atomic< uint64_t > phead_{};
        alignas(64) mutable std::atomic< uint64_t > chead_{};

        std::pair< cursor, block* > get_block(std::atomic< uint64_t >& head) const
        {
            auto value = cursor(head.load());
            return { value, &blocks_[value.offset & (blocks_.size() - 1)] };
        }
        
        status advance_phead(cursor head)
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto consumed = cursor(next_block.consumed.load());
            if (consumed.version < head.version ||
                (consumed.version == head.version && consumed.offset != BlockSize))
            {
                auto reserved = cursor(next_block.reserved.load());
                if (reserved.offset == consumed.offset)
                    return status::fail;
                else
                    return status::busy;
            }
            // TODO: drop-old mode
            //auto committed = Cursor(next_block.committed.load());
            //if (commited.version == head.version && commited.index != BlockSize)
            //    return advance_status_type::not_available;
            atomic_fetch_max_explicit(&next_block.committed, (uint64_t)cursor(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.allocated, (uint64_t)cursor(0, head.version + 1));

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&phead_, (uint64_t)cursor(head.offset + 1, head.version));
            return status::success;
        }

        bool advance_chead(cursor head, uint32_t version) const
        {
            auto& next_block = blocks_[(head.offset + 1) & (blocks_.size() - 1)];
            auto committed = cursor(next_block.committed.load());
            if (committed.version != head.version + 1)
                return false;
            atomic_fetch_max_explicit(&next_block.consumed, (uint64_t)cursor(0, head.version + 1));
            atomic_fetch_max_explicit(&next_block.reserved, (uint64_t)cursor(0, head.version + 1));

            // TODO: drop-old mode
            //if (committed.version < version + (head.index == 0))
            //    return false;
            //atomic_fetch_and_max(next_block.reserved, { 0, committed.version });

            // TODO: how does the article handle wrap-around?
            if (((head.offset + 1) & (blocks_.size() - 1)) == 0)
                ++head.version;

            atomic_fetch_max_explicit(&chead_, (uint64_t)cursor(head.offset + 1, head.version));
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
            this->destroy_blocks(blocks_);
        }

        template< typename... Args > bool emplace(Args&&... args)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(phead_);
                auto status = this->emplace_block(block, std::forward< Args >(args)...);
                switch (status)
                {
                case status::success:
                    return true;
                case status::block_done:
                    switch (advance_phead(head))
                    {
                    case status::success: continue;
                    case status::busy: //break;
                    case status::fail: return false;
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

        template< typename U = T, typename Result >
        std::enable_if_t< detail::is_trivial_v< U >, bool > pop(Result& result)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(chead_);
                auto [status, entry] = this->reserve_entry(block, backoff);
                switch (status)
                {
                case status::success:
                    this->consume_entry(block, entry, result);
                    return true;
                case status::busy: //break;
                case status::fail: return false;
                case status::block_done:
                    if (!advance_chead(head, entry.version))
                        return false;
                    continue;
                default:
                    assert(false);
                }

                backoff();
            }
        }

        template< typename U = T, typename Result >
        std::enable_if_t< !detail::is_trivial_v< U >, bool > pop(Result& result)
        {
            Backoff backoff;
            while (true)
            {
                auto [head, block] = get_block(chead_);
                auto [status, entry] = this->reserve_entry(block, backoff);
                switch (status)
                {
                case status::success:
                    if (this->consume_entry(block, entry, result)) return true; else continue;
                case status::busy: //break;
                case status::fail: return false;
                case status::block_done:
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
                auto reserved = cursor(block->reserved.load());
                if (reserved.offset < BlockSize)
                {
                    auto committed = cursor(block->committed.load());
                    if (committed.offset == reserved.offset)
                        return true;

                    return this->empty_check(block, reserved, committed);
                }
                else if (!advance_chead(head, reserved.version))
                {
                    return false;
                }
            }
        }
    };
}
