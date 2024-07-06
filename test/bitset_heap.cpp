//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/bitset_heap.h>

#include <gtest/gtest.h>

TEST(bitset_heap_test, basic) {
    elasticheap::detail::bitset_heap<uint32_t, 256> heap;
    ASSERT_TRUE(heap.empty());
    for(std::size_t i = 0; i < heap.capacity(); ++i) {
        heap.push(i);
        ASSERT_EQ(heap.size(), i + 1);
    }

    ASSERT_FALSE(heap.empty());

    for(std::size_t i = 0; i < heap.capacity(); ++i) {
        ASSERT_EQ(heap.pop(), i);
    }
}