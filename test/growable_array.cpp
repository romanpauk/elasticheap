//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/growable_array.h>

#include <gtest/gtest.h>

TEST(growable_array, basics) {
    containers::growable_array<size_t, std::allocator<size_t>, 1 > array;
    decltype(array)::reader_state state;

    for(int i = 0; i < 3; ++i) {
        ASSERT_EQ(array.empty(), true);
        ASSERT_EQ(array.size(), 0);
        ASSERT_EQ(array.push_back(1), 1);
        ASSERT_EQ(array.empty(), false);
        ASSERT_EQ(array.size(), 1);
        ASSERT_EQ(array[0], 1);
        ASSERT_EQ(array.read(state, 0), 1);

        ASSERT_EQ(array.push_back(2), 2);
        ASSERT_EQ(array.empty(), false);
        ASSERT_EQ(array.size(), 2);
        ASSERT_EQ(array[1], 2);
        ASSERT_EQ(array.read(state, 1), 2);

        array.clear();
    }
}

TEST(growable_array, emplace_back_trivial) {
    containers::growable_array<size_t> array;
    for (size_t i = 0; i < 10000; ++i) {
        array.emplace_back(i);

        for (size_t j = 0; j < i; ++j) {
            ASSERT_EQ(array[j], j);
        }
    }
}

TEST(growable_array, emplace_back_nontrivial) {
    containers::growable_array<std::string> array;
    for (size_t i = 0; i < 10000; ++i) {
        array.emplace_back(std::to_string(i));

        for (size_t j = 0; j < i; ++j) {
            ASSERT_EQ(array[j], std::to_string(j));
        }
    }
}
