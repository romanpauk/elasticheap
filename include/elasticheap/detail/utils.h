//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <stdio.h>

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

