//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/lockfree/atomic16.h>

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
