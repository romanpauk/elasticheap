//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/bitset.h>

#include <gtest/gtest.h>

using bitset_types = testing::Types<
    elasticheap::detail::bitset<8>,
    elasticheap::detail::bitset<16>,
    elasticheap::detail::bitset<32>,
    elasticheap::detail::bitset<64>,
    elasticheap::detail::bitset<128>,
    elasticheap::detail::bitset<256>
>;

template <typename T> struct bitset_test : public testing::Test {};
TYPED_TEST_SUITE(bitset_test, bitset_types);

TYPED_TEST(bitset_test, basic) {
    TypeParam bitset;
    bitset.clear();
    ASSERT_TRUE(bitset.empty());
    ASSERT_FALSE(bitset.full());
    ASSERT_FALSE(bitset.get(0));

    bitset.set(0);
    ASSERT_FALSE(bitset.empty());
    ASSERT_FALSE(bitset.full());
    ASSERT_TRUE(bitset.get(0));
    bitset.clear(0);
    ASSERT_FALSE(bitset.get(0));

    for (size_t i = 0; i < bitset.size(); ++i) {
        ASSERT_FALSE(bitset.get(i));
        bitset.set(i);
        ASSERT_TRUE(bitset.get(i));
    }
    ASSERT_TRUE(bitset.full());
}