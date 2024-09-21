#include <elasticheap/allocator.h>

// See https://www.gnu.org/software/libc/manual/html_node/Replacing-malloc.html

void* malloc(size_t size);
void* calloc(size_t size1, size_t size2);
void* realloc(void* start, size_t size); // TODO: should be possible to realloc inside arena
void free(void* memory);

//aligned_alloc
//malloc_usable_size
//memalign
//posix_memalign
//pvalloc
//valloc

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
