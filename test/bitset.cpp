//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/bitset.h>

#include <gtest/gtest.h>

TEST(bitset_test, basic) {
    elasticheap::detail::bitset<8> bitset;
    bitset.clear();
    ASSERT_TRUE(bitset.empty());
    ASSERT_FALSE(bitset.full());
    ASSERT_FALSE(bitset.get(0));

    bitset.set(0);
    ASSERT_FALSE(bitset.empty());
    ASSERT_TRUE(bitset.get(0));
}