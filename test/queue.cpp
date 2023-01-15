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
    , containers::unbounded_queue< int >
    , containers::bounded_queue_bbq< int, queue_size * 2 > // It is not possible to push into block that has unconsumed data
>;

template <typename T> struct queue_test : public testing::Test {};
TYPED_TEST_SUITE(queue_test, queue_types);

TYPED_TEST(queue_test, basic_operations)
{
    using container = TypeParam;

    container c;

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
        }

        for (size_t j = 0; j < i; ++j)
        {
            typename container::value_type v = -1;
            ASSERT_TRUE(c.pop(v));
            ASSERT_EQ(v, j);
        }
    }
}


