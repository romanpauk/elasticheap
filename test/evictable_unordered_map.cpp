//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/evictable_unordered_map.h>

#include <gtest/gtest.h>

TEST(lru, basic_operations) {

    containers::evictable_unordered_map< int, int > cache;
    ASSERT_EQ(cache.evictable(), cache.end());
    cache.emplace(1, 100);
    ASSERT_EQ(cache.evictable()->first, 1);
    cache.emplace(2, 200);
    ASSERT_EQ(cache.evictable()->first, 1);
    cache.emplace(3, 300);
    ASSERT_EQ(cache.evictable()->first, 1);
    cache.touch(cache.find(1));
    ASSERT_EQ(cache.evictable()->first, 2);
    cache.erase(cache.evictable());
    ASSERT_EQ(cache.evictable()->first, 3);
    cache.erase(cache.evictable());
    ASSERT_EQ(cache.evictable()->first, 1);
}
