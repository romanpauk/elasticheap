//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <containers/allocators/arena_allocator.h>
#include <containers/allocators/arena_allocator2.h>
#include <containers/allocators/page_allocator.h>

#include <gtest/gtest.h>

TEST(arena_allocator_test, std_allocator) {
    uint8_t buffer[128];
    containers::arena<> arena(buffer);
    containers::arena_allocator< char > allocator(arena);

    allocator.allocate(128);
}

TEST(arena_allocator_test, page_allocator) {
    uint8_t buffer[128];
    containers::arena< containers::page_allocator<char> > arena(buffer, sizeof(buffer));
    containers::arena_allocator< char, decltype(arena) > allocator(arena);

    allocator.allocate(128);
}
