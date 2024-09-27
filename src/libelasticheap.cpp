//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <elasticheap/allocator.h>

// See https://www.gnu.org/software/libc/manual/html_node/Replacing-malloc.html

//#define TRACE

#if defined(TRACE)
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG(...)
#endif

using allocator_type = elasticheap::allocator<uint8_t>;

void* malloc(size_t size) {
    allocator_type alloc;
    void* p = alloc.allocate(size);
    DEBUG("%p = malloc(%lu)\n", p, size);
    return p;
}

void* calloc(size_t size1, size_t size2) {
    allocator_type alloc;
    auto bytes = std::max<size_t>(size1 * size2, 1);
    void* p = alloc.allocate(bytes);
    memset(p, 0, bytes);
    DEBUG("%p = calloc(%lu, %lu)\n", p, size1, size2);
    return p;
}

void* realloc(void* ptr, size_t size) {
    allocator_type alloc;
    if (__likely(ptr != 0)) {
        void* p = alloc.reallocate((uint8_t*)ptr, size);
        DEBUG("%p = realloc(%p, %lu)\n", ptr, p, size);
        return p;
    }

    void* p = alloc.allocate(size);
    DEBUG("%p = realloc(0, %lu)\n", p, size);
    return p;
}

void free(void* ptr) {
    allocator_type alloc;
    DEBUG("free(%p)\n", ptr);
    alloc.deallocate((uint8_t*)ptr, 0);
}

//aligned_alloc
//malloc_usable_size
//memalign
//posix_memalign
//pvalloc
//valloc

#if 0
// https://en.cppreference.com/w/cpp/memory/new/operator_new
void* operator new(std::size_t count);
void* operator new[](std::size_t count);
void* operator new(std::size_t count, std::align_val_t al);     // since C++17
void* operator new[](std::size_t count, std::align_val_t al);   // since C++17
// ----
void* operator new(std::size_t count, const std::nothrow_t&) noexcept;
void* operator new[](std::size_t count, const std::nothrow_t&) noexcept;
void* operator new(std::size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept; // since C++17
void* operator new[](std::size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept; // since C++17

// https://en.cppreference.com/w/cpp/memory/new/operator_delete
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, std::align_val_t al) noexcept;      // since C++17
void operator delete[](void* ptr, std::align_val_t al) noexcept;    // since C++17
// ----
void operator delete(void* ptr, std::size_t sz) noexcept;       // since C++14
void operator delete[](void* ptr, std::size_t sz) noexcept;     // since C++14
void operator delete(void* ptr, std::size_t sz, std::align_val_t al) noexcept;  // since C++17
void operator delete[](void* ptr, std::size_t sz, std::align_val_t al ) noexcept;   // since C++17
void operator delete(void* ptr, const std::nothrow_t&) noexcept;    // since C++17
void operator delete[](void* ptr, const std::nothrow_t&) noexcept;  // since C++17
void operator delete(void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept;   // since C++17
void operator delete[](void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept; // since C++17
#endif
