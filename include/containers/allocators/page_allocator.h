//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace containers {

template <typename T> class page_allocator {
public:
    using value_type = T;

    page_allocator() = default;
    template < typename U > page_allocator(const page_allocator<U>&) noexcept {}
        
    T* allocate(std::size_t n) {
    #if defined(_WIN32)
        return reinterpret_cast<T*>(VirtualAlloc(0, sizeof(T) * n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    #else
        return reinterpret_cast<T*>(mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    #endif
    }

    void deallocate(T* p, std::size_t n) noexcept {
    #if defined(_WIN32)
        (void)n;
        VirtualFree(p, 0, MEM_RELEASE);
    #else
        munmap(p, n);
    #endif
    }
};

template <typename T, typename U>
bool operator == (const page_allocator<T>&, const page_allocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
bool operator != (const page_allocator<T>& x, const page_allocator<U>& y) noexcept {
    return !(x == y);
}

template< typename T > struct arena_allocator_traits;
template< typename T > struct arena_allocator_traits<page_allocator<T>> {
    static constexpr std::size_t header_size() { return 0; }
};

}