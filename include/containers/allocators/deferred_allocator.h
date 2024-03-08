//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <cassert>
#include <memory>

namespace containers::detail {
    template< 
        typename T, 
        typename Allocator, 
        typename AllocatorBase = typename std::allocator_traits<Allocator>::template rebind_alloc<uint8_t> 
    > class deferred_allocator: public AllocatorBase {
        struct buffer {
            buffer* next = nullptr;
            size_t size = 0;
        };

        template< typename U > struct stack {
            void push(U* value) {
                assert(value);
                value->next = head_.next;
                head_.next = value;
            }

            U* top() {
                return head_.next; 
            }

            U* pop() {
                U* value = head_.next;
                head_.next = value ? value->next : nullptr;
                return value;
            }

        private:
            U head_;
        };

        buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - sizeof(buffer));
        }

        stack<buffer> stack_;

    public:
        using value_type = T;

        ~deferred_allocator() {
            reset();
        }

        T* allocate(size_t n) {
            static_assert(sizeof(buffer) == 16);
            buffer* ptr = (buffer*)AllocatorBase::allocate(sizeof(buffer) + sizeof(T) * n);
            ptr->next = nullptr;
            ptr->size = sizeof(buffer) + sizeof(T) * n;
            return reinterpret_cast<T*>(ptr + 1);
        }
        
        void deallocate(T* ptr, size_t) {
            stack_.push(buffer_cast(ptr));
        }

        void reset() {
            while(auto ptr = stack_.pop())
                AllocatorBase::deallocate((uint8_t*)ptr, ptr->size);
        }
    };
}
