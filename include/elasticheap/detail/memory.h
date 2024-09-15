//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/utils.h>

#include <sys/mman.h>

namespace elasticheap::detail {
    struct memory {
        static void* reserve(std::size_t size) {
            void* p = mmap(0, size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (p == MAP_FAILED)
                __failure("mmap");
            return p;
        }

        static bool commit(void* ptr, std::size_t size) {
            if (mprotect(ptr, size, PROT_READ | PROT_WRITE) != 0)
                __failure("mprotect");
            return true;
        }

        static bool decommit(void* ptr, std::size_t size) {
            //madvise(ptr, size, MADV_DONTNEED);
            void* p = mmap(ptr, size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (p == MAP_FAILED)
                __failure("mmap");
            return true;
        }

        static void free(void* ptr, std::size_t size) {
            if (munmap(ptr, size) != 0)
                __failure("munmap");
        }
    };
}
