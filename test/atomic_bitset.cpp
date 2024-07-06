//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/atomic_bitset.h>

#include <gtest/gtest.h>

using bitset_types = testing::Types<
    elasticheap::detail::atomic_bitset<8>,
    elasticheap::detail::atomic_bitset<16>,
    elasticheap::detail::atomic_bitset<32>,
    elasticheap::detail::atomic_bitset<64>,
    elasticheap::detail::atomic_bitset<128>,
    elasticheap::detail::atomic_bitset<256>
>;

template <typename T> struct atomic_bitset_test : public testing::Test {};
TYPED_TEST_SUITE(atomic_bitset_test, bitset_types);

TYPED_TEST(atomic_bitset_test, sizes) {
    static_assert(sizeof(TypeParam) == TypeParam::size()/8);
}

TYPED_TEST(atomic_bitset_test, basic) {
    TypeParam bitset;
    bitset.clear();
    for (size_t i = 0; i < bitset.size(); ++i) {
        ASSERT_FALSE(bitset.get(0));
    }

    bitset.set(0);
    ASSERT_TRUE(bitset.get(0));
    bitset.clear(0);
    ASSERT_FALSE(bitset.get(0));

    for (size_t i = 0; i < bitset.size(); ++i) {
        ASSERT_FALSE(bitset.get(i));
        bitset.set(i);
        ASSERT_TRUE(bitset.get(i));
    }
}