//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/detail/atomic_bitset_heap.h>

#include <gtest/gtest.h>

TEST(atomic_bitset_heap_test, basic) {
    std::atomic<uint64_t> range;
    elasticheap::detail::atomic_bitset_heap<uint32_t, 256> heap(range);
    ASSERT_TRUE(heap.empty(range));
    for(std::size_t i = 0; i < heap.capacity(); ++i) {
        heap.push(range, i);
        ASSERT_FALSE(heap.empty(range));
    }

    for(std::size_t i = 0; i < heap.capacity(); ++i) {
        uint32_t value;
        ASSERT_TRUE(heap.pop(range, value));
        ASSERT_EQ(value, i);
    }

    ASSERT_TRUE(heap.empty(range));
}
