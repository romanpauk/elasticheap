//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <gtest/gtest.h>

#include <containers/lockfree/unbounded_stack.h>
#include <containers/lockfree/bounded_stack.h>

const int stack_size = 128;

using stack_types = ::testing::Types<
    containers::bounded_stack< int, stack_size >
    , containers::unbounded_stack< int >
    , containers::unbounded_blocked_stack< int >  
>;

template <typename T> struct stack_test : public testing::Test {};
TYPED_TEST_SUITE(stack_test, stack_types);

TYPED_TEST(stack_test, basic_operations)
{
    using container = TypeParam;
    container c;
    
    for (size_t i = 1; i <= stack_size; ++i)
    {
        

        for (size_t j = 0; j < i; ++j)
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

        for (int j = i; j > 0; --j)
        {
            typename container::value_type v = -1;
            ASSERT_TRUE(c.pop(v)) << j;
            ASSERT_EQ(v, j - 1);
        }
    }
}
