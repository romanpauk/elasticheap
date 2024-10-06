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

//template< std::size_t N > elasticheap::allocator<std::array<uint8_t, N>>* get_allocator() {
//    static elasticheap::allocator<std::array<uint8_t, N>> instance;
//    return &instance;
//}

static allocator_type* ptr;
allocator_type& get_allocator() {
    //return allocator_type();
    //static allocator_type instance;
    //return instance;
    /*if (__unlikely(!ptr)) {
        ptr = []{
            static allocator_type instance;
            return &instance;
        }();
    }
    return ptr;
    */
    static allocator_type* ptr = []{
        static allocator_type instance;
        return &instance;
    }();
    return *ptr;
}

void* malloc(size_t size) {
    void* p = get_allocator().allocate(size);
    DEBUG("%p = malloc(%lu)\n", p, size);
    return p;
}

void* calloc(size_t size1, size_t size2) {
    auto bytes = std::max<size_t>(size1 * size2, 1);
    void* p = get_allocator().allocate(bytes);
    memset(p, 0, bytes);
    DEBUG("%p = calloc(%lu, %lu)\n", p, size1, size2);
    return p;
}

void* realloc(void* ptr, size_t size) {
    if (__likely(ptr != 0)) {
        void* p = get_allocator().reallocate((uint8_t*)ptr, size);
        DEBUG("%p = realloc(%p, %lu)\n", ptr, p, size);
        return p;
    }

    void* p = get_allocator().allocate(size);
    DEBUG("%p = realloc(0, %lu)\n", p, size);
    return p;
}

void free(void* ptr) {
    DEBUG("free(%p)\n", ptr);
    get_allocator().deallocate((uint8_t*)ptr, 0);
}

//aligned_alloc
//malloc_usable_size
//memalign
//posix_memalign
//pvalloc
//valloc

// https://en.cppreference.com/w/cpp/memory/new/operator_new
void* operator new(std::size_t count) { return malloc(count); }
void* operator new[](std::size_t count) { return malloc(count); }
void* operator new(std::size_t count, std::align_val_t al) { return malloc(count); }     // since C++17
void* operator new[](std::size_t count, std::align_val_t al) { return malloc(count); }   // since C++17
// ----
/*
void* operator new(std::size_t count, const std::nothrow_t&) noexcept;
void* operator new[](std::size_t count, const std::nothrow_t&) noexcept;
void* operator new(std::size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept; // since C++17
void* operator new[](std::size_t count, std::align_val_t al, const std::nothrow_t& tag) noexcept; // since C++17
*/

// https://en.cppreference.com/w/cpp/memory/new/operator_delete
void operator delete(void* ptr) noexcept { return free(ptr); }
void operator delete[](void* ptr) noexcept { return free(ptr); }
void operator delete(void* ptr, std::align_val_t al) noexcept { return free(ptr); }      // since C++17
void operator delete[](void* ptr, std::align_val_t al) noexcept { return free(ptr); }    // since C++17
// ----
/*
void operator delete(void* ptr, std::size_t sz) noexcept;       // since C++14
void operator delete[](void* ptr, std::size_t sz) noexcept;     // since C++14
void operator delete(void* ptr, std::size_t sz, std::align_val_t al) noexcept;  // since C++17
void operator delete[](void* ptr, std::size_t sz, std::align_val_t al ) noexcept;   // since C++17
void operator delete(void* ptr, const std::nothrow_t&) noexcept;    // since C++17
void operator delete[](void* ptr, const std::nothrow_t&) noexcept;  // since C++17
void operator delete(void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept;   // since C++17
void operator delete[](void* ptr, std::align_val_t al, const std::nothrow_t&) noexcept; // since C++17
*/
