//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#include <containers/growable_array.h>

#include <gtest/gtest.h>

TEST(growable_array, basics) {
    containers::growable_array<int> array;
    for (size_t i = 0; i < 10000; ++i) {
        array.push_back(i);

        for (size_t j = 0; j < i; ++j) {
            ASSERT_EQ(array[j], j);
        }
    }
}
