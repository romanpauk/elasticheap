//  
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <gtest/gtest.h>

#include <containers/lockfree/unbounded_queue.h>
#include <containers/lockfree/bounded_queue.h>
#include <containers/lockfree/bounded_queue_bbq.h>

const int queue_size = 128; // TODO: the calculation in bbq queue needs at least 128, investigate

using queue_types = ::testing::Types<
    containers::bounded_queue< int, queue_size >
    , containers::bounded_queue_bbq< int, queue_size * 2 > // It is not possible to push into block that has unconsumed data
    , containers::unbounded_queue< int >
    , containers::unbounded_blocked_queue< int > // It is not possible to push into block that has unconsumed data
>;

template <typename T> struct queue_test : public testing::Test {};
TYPED_TEST_SUITE(queue_test, queue_types);

TYPED_TEST(queue_test, basic_operations)
{
    using container = TypeParam;

    container c;

    ASSERT_TRUE(c.empty());
    for (size_t i = 1; i <= queue_size; ++i)
    {
        for(size_t j = 0; j < i; ++j)
        {
            if constexpr (std::is_same_v< decltype(c.push(j)), bool >)
            {
                ASSERT_TRUE(c.push(j));
            }
            else
            {
                c.push(j);
            }

            ASSERT_TRUE(!c.empty());
        }

        for (size_t j = 0; j < i; ++j)
        {
            typename container::value_type v = -1;
            ASSERT_TRUE(c.pop(v));
            ASSERT_EQ(v, j);
        }

        ASSERT_TRUE(c.empty());
    }
}

TEST(bounded_queue_bbq_test, nondefault_entry)
{
    size_t dtors{};

    struct nondefault
    {
        nondefault(size_t* counter)
            : counter_(counter)
        {}

        nondefault(const nondefault&) = delete;

        nondefault(nondefault&& other)
            : counter_(other.counter_)
        {
            other.counter_ = 0;
        }
      
        nondefault& operator = (nondefault&& other)
        {
            counter_ = other.counter_;
            other.counter_ = 0;
            return *this;
        }

        nondefault& operator = (const nondefault& other) = delete;

        ~nondefault() { if(counter_) ++*counter_; }

    private:
        size_t* counter_;
    };

    {
        containers::bounded_queue_bbq< nondefault, queue_size * 2 > queue;
        queue.emplace(&dtors);
        queue.emplace(&dtors);
    }

    ASSERT_EQ(dtors, 2);
    dtors = 0;

    {
        std::optional< nondefault > tmp;
        {
            containers::bounded_queue_bbq< nondefault, queue_size * 2 > queue;
            queue.emplace(&dtors);
            queue.emplace(&dtors);
            ASSERT_EQ(queue.pop(tmp), true);
            ASSERT_EQ(dtors, 0);
        }

        ASSERT_EQ(dtors, 1); // One element left in queue
    }
    ASSERT_EQ(dtors, 2); // ~tmp
}
