//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/lockfree/counter.h>

#include <gtest/gtest.h>

using counter_types = ::testing::Types<
    containers::counter< uint64_t, 32 >
    , containers::counter< std::atomic< uint64_t >, 32 >
>;

template <typename T> struct counter_test : public testing::Test {};
TYPED_TEST_SUITE(counter_test, counter_types);

TYPED_TEST(counter_test, basic_operations) {
    using counter_type = TypeParam;
    counter_type counter;
    counter.inc(1, 1);
    counter.inc(1, 2);
    ASSERT_EQ(counter.get(), 2);
}
