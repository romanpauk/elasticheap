//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <atomic>

namespace containers::detail
{    
    template< typename T, typename Allocator = std::allocator< T > > class leaking_allocator
    {
        struct empty_guard {};

        template< typename U, typename AllocatorU > friend class leaking_allocator;

        static_assert(std::is_empty_v< Allocator >);
        
        using allocator_type = Allocator;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

    public:
        template< typename U > struct rebind
        {
            using other = leaking_allocator< U, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };
        
        leaking_allocator() {};
        template< typename U, typename AllocatorT > leaking_allocator(leaking_allocator< U, AllocatorT >&) {}

        auto guard() { return empty_guard(); }
        
        template< typename... Args > T* allocate(Args&&... args)
        {
            allocator_type allocator;
            auto ptr = allocator_traits_type::allocate(allocator, 1);
            allocator_traits_type::construct(allocator, ptr, std::forward< Args >(args)...);
            return ptr;
        }

        T* protect(std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst)
        {
            return value.load(order);
        }

        void retire(T* ptr) {}

        void deallocate(T* ptr)
        {
            //allocator_type allocator;
            //allocator_traits_type::destroy(allocator, ptr);
            //allocator_traits_type::deallocate(allocator, ptr, 1);
        }
    };
}
