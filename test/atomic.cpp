//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/lockfree/atomic.h>

#include <gtest/gtest.h>

TEST(atomic16_test, basic_operations)
{
    struct data
    {
        uint64_t a;
        uint64_t b;
    };
    
    containers::atomic16< data > a;
    a.store({1, 1});

    {
        data expected{ 2, 2 };
        ASSERT_FALSE(a.compare_exchange_strong(expected, data{ 3, 3 }));
    }

    {
        data expected{ 1, 1 };
        ASSERT_TRUE(a.compare_exchange_strong(expected, data{ 1, 3 }));
    }
}

TEST(atomic_fetch_max_explicit, test)
{
    std::atomic< int > a(1);
    ASSERT_EQ(containers::atomic_fetch_max_explicit(&a, 1), 1);
    ASSERT_EQ(a.load(), 1);
    ASSERT_EQ(containers::atomic_fetch_max_explicit(&a, 2), 1);
    ASSERT_EQ(a.load(), 2);
}
