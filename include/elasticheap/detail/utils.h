//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <stdio.h>
#include <cstdint>

#if 0
#undef assert
#define assert(exp) \
    do { \
        if(!(exp)) { \
            fprintf(stderr, "%s: %d: Assertion failed: " #exp "\n", __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0);
#endif

#define __assume(cond) do { if (!(cond)) __builtin_unreachable(); } while (0)
#define __likely(cond) __builtin_expect((cond), true)
#define __unlikely(cond) __builtin_expect((cond), false)
#define __failure(msg) do { fprintf(stderr, "%s:%d: %s: %s\n", \
    __FILE__, __LINE__, __PRETTY_FUNCTION__, msg); } while(0)

#define __forceinline __attribute__((always_inline))

inline bool is_ptr_aligned(void* ptr, std::size_t alignment) {
    return ((uintptr_t)ptr & (alignment - 1)) == 0;
}

inline bool is_ptr_in_range(void* ptr, std::size_t size, void* begin, void* end) {
    return (uintptr_t)ptr >= (uintptr_t)begin && (uintptr_t)ptr + size <= (uintptr_t)end;
}

template < std::size_t Alignment, typename T > T* align(T* ptr) {
    static_assert((Alignment & (Alignment - 1)) == 0);
    return (T*)(((uintptr_t)ptr + Alignment - 1) & ~(Alignment - 1));
}

template < std::size_t Alignment, typename T > T* mask(T* ptr) {
    static_assert((Alignment & (Alignment - 1)) == 0);
    return (T*)((uintptr_t)ptr & ~(Alignment - 1));
}

static constexpr uint32_t round_up(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}


