//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

//
// Snapshot-Free, Transparent, and Robust Memory Reclamation for Lock-Free Data Structures
// https://arxiv.org/pdf/1905.07903
//

#include <containers/lockfree/detail/aligned.h>
#include <containers/lockfree/detail/thread_manager.h>

#include <cassert>
#include <array>
#include <vector>
#include <atomic>
#include <algorithm>

// TODO: not finished, just for perf testing
// It is quite badly implemented single CAS hyaline version from the article.

#define DEBUG_(...)
//#define DEBUG_(...) fprintf(stderr, __VA_ARGS__)

namespace containers::detail
{
    // TODO: this is a mpsc bounded queue, but it is faster than my queues
    template< typename T, int64_t Size, typename Backoff > class free_list_stack {
        static_assert(std::is_trivial_v< T >);

        std::atomic< T* > head_{};
        std::atomic< int32_t > size_{};

    public:
        ~free_list_stack() {
            assert(head_.load(std::memory_order_relaxed) == 0);
        }
        
        T* pop() {
            Backoff backoff;
            while (true) {
                auto head = head_.load(std::memory_order_relaxed);
                if (!head)
                    return nullptr;
                
                if (head_.compare_exchange_weak(head, head->next)) {
                    auto size = size_.fetch_sub(1, std::memory_order_relaxed);
                    assert(size >= 0);
                    return head;
                }

                backoff();
            }
        }

        bool push(T* head) {
            if (size_.load(std::memory_order_relaxed) > Size)
                return false;
            
            Backoff backoff;
            while (true) {
                head->next = head_.load(std::memory_order_relaxed);
                if(head_.compare_exchange_weak(head->next, head)) {
                    auto size = size_.fetch_add(1, std::memory_order_relaxed);
                    assert(size >= 0);
                    return true;
                }
                backoff();
            }
        }

        template< typename Allocator > void clear(Allocator& alloc) {
            T* head = head_.load();
            while (head) {
                T* next = head->next;
                std::allocator_traits< Allocator >::deallocate(alloc, head, 1);
                head = next;
            }
        }
    };

#if 0
    template< typename T, int64_t Size, typename Backoff > class free_list_queue {
        static_assert(std::is_trivial_v< T >);
        bounded_queue_bbq< T*, Size > queue_;

    public:
        T* pop() {
            T* value = nullptr;
            queue_.pop(value);
            return value;
        }

        bool push(T* head) {
            return queue_.push(head);
        }

        template< typename Allocator > void clear(Allocator& alloc) {}
    };
#endif

    template< size_t Size, size_t Alignment > struct free_list_node {
        union {
            std::aligned_storage_t< Size, Alignment > data;
            free_list_node< Size, Alignment >* next;
        };
    };

    // Factored out so it could be static and shared between different allocators
    template< typename T, size_t N, typename Backoff > class free_list_allocator_base {
    public:
        alignas(64) std::array< aligned< free_list_stack< T, 64, Backoff > >, N > free_lists_;
    };

    // template< typename T, size_t N, typename Backoff > alignas(64) std::array< aligned< free_list_stack< T, 64, Backoff > >, N > free_list_allocator_base< T, N, Backoff >::free_lists_;

    template< typename T, size_t N, typename Backoff = detail::exponential_backoff<>, typename Allocator = std::allocator< T > > class free_list_allocator
        : free_list_allocator_base< free_list_node< sizeof(T), alignof(T) >, N, Backoff >
    {
        using node_type = free_list_node< sizeof(T), alignof(T) >;
        
        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< node_type >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;

        static node_type* node_cast(T* ptr) {
            return reinterpret_cast<node_type*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(node_type, data));
        }

    public:
        using value_type = T;

        ~free_list_allocator() {
            // TODO: In case it it static, we can't clear it
            for(auto& list: this->free_lists_)
                list.clear(allocator_);
        }

        T* allocate(size_t n, size_t id) {
            assert(n == 1);
            auto node = this->free_lists_[id].pop();
            if (!node) {
                node = allocator_traits_type::allocate(allocator_, 1);
            }

            return reinterpret_cast< T* >(&node->data);
        }

        void deallocate(T* ptr, size_t n, size_t id) {
            assert(n == 1);
            auto node = node_cast(ptr);
            if (!this->free_lists_[id].push(node)) {
                allocator_traits_type::deallocate(allocator_, node, 1);
            }
        }
    };

    template<
        typename T,
        typename Allocator = std::allocator< T >,
        typename ThreadManager = thread,
        size_t N = ThreadManager::max_threads
    > class hyaline_allocator {
        using hyaline_allocator_type = hyaline_allocator< T, Allocator, ThreadManager >;

        static constexpr size_t Adjs = uint64_t(-1) / N + 1; // Needed for different Hyaline version

        struct node_t {
            std::atomic< int64_t > ref;
            // TODO: this should be a buffer of allocated objects
        };

        struct node_list_t {
            std::atomic< node_list_t* > next;
            size_t id;
            node_t* node;
        };

        //static_assert(std::is_trivial_v< node_list_t >);

        struct head_t {
            head_t() = default;
            head_t(node_list_t* node, uint64_t ref) {
                address = reinterpret_cast<uintptr_t>(node) | ref;
            }

            node_list_t* get_ptr() { return reinterpret_cast<node_list_t*>(address & ~1); }
            uint64_t get_ref() { return address & 1; }

        private:
            union {
                node_list_t* ptr;
                uintptr_t address;
            };
        };

        alignas(64) free_list_allocator< node_list_t, N > node_lists_;
        alignas(64) std::array< aligned< std::atomic< head_t > >, N > heads_;

        struct guard_class {
            guard_class() = default;
            guard_class(hyaline_allocator_type* allocator, size_t id)
                : allocator_(allocator)
                , id_(id & (N - 1))
                , end_(allocator->enter(id_)) {}

            ~guard_class() { allocator_->leave(id_, end_); }

        private:
            hyaline_allocator_type* allocator_;
            size_t id_;
            node_list_t* end_;
        };

        node_list_t* enter(size_t id) {
            heads_[id].store({ nullptr, 1 }, std::memory_order_release);
            return nullptr;
        }

        void leave(size_t id, node_list_t* node) {
            auto head = heads_[id].exchange({ nullptr, 0 }, std::memory_order_acq_rel); // synchronize with retire()
            if (head.get_ptr() != nullptr)
                traverse(head.get_ptr(), node);
        }

        void traverse(node_list_t* node, node_list_t* end) {
            DEBUG_("[%llu] traverse(): %p\n", thread::id(), node);
            node_list_t* current = nullptr;
            do {
                current = node;
                if (!current)
                    break;
                node = current->next;
                if (current->node->ref.fetch_add(-1) == 1) {
                    free(current);
                }
            } while (current != end);
        }

        void retire(node_t* node) {
            int inserts = 0;
            node->ref = 0;
            
            auto id = thread::id();
            for (size_t i = 0; i < heads_.size(); ++i) {
                head_t head{};
                head_t new_head{};

                auto n = node_lists_.allocate(1, id);
                // TODO: construct node_list_t
                // std::allocator_traits< free_list_allocator< node_list_t, N > >::construct(node_lists_, n, nullptr, id, node);
                n->id = id;
                n->next = nullptr;
                n->node = node;

                do {
                    head = heads_[i].load(std::memory_order_acquire); // synchronize with enter()
                    if (head.get_ref() == 0)
                        goto next;
        
                    new_head = head_t(n, head.get_ref());
                    n->next = head.get_ptr();
                } while (!heads_[i].compare_exchange_strong(head, new_head, std::memory_order_acq_rel)); // synchronize with leave()
                ++inserts;
            next:
                ;
            }

            adjust(node, inserts);
        }

        void adjust(node_t* node, int value) {
            if (node->ref.fetch_add(value) == -value) {
                // TODO: How is this supposed to work if it frees the node but not removes it from the list?
                // Need to understand this case. For single CAS version, it should never deallocate here.
                std::abort();
                //free(node);
            }
        }

        void free(node_list_t* node) {
            deallocate(buffer_cast(node->node));

            // TODO: destroy node_list_t
            node_lists_.deallocate(node, 1, node->id);
        }
        
        struct buffer {
            node_t node{};
            std::aligned_storage_t< sizeof(T), alignof(T) > data;
        };

        using allocator_type = typename std::allocator_traits< Allocator >::template rebind_alloc< buffer >;
        using allocator_traits_type = std::allocator_traits< allocator_type >;

        allocator_type allocator_;

        static buffer* buffer_cast(T* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, data));
        }

        static buffer* buffer_cast(node_t* ptr) {
            return reinterpret_cast<buffer*>(reinterpret_cast<uintptr_t>(ptr) - offsetof(buffer, node));
        }

        void deallocate(buffer* ptr) {
            reinterpret_cast<T*>(&ptr->data)->~T();
            allocator_traits_type::destroy(allocator_, ptr);
            allocator_traits_type::deallocate(allocator_, ptr, 1);
        }

    public:
        using value_type = T;

        template< typename U > struct rebind {
            using other = hyaline_allocator< U, typename std::allocator_traits< Allocator >::template rebind_alloc< U > >;
        };

        hyaline_allocator() = default;
        hyaline_allocator(const hyaline_allocator&) = delete;
        hyaline_allocator(hyaline_allocator&&) = delete;
        
        // TODO: in some cases using token() is a bit faster (sometimes around 10%). On the other hand non-sequential
        // id requires DCAS later. For simplicity, try to finish this with id() and see.
        auto guard() { return guard_class(this, ThreadManager::id()); }

        T* allocate(size_t n) {
            assert(n == 1);
            auto ptr = allocator_traits_type::allocate(allocator_, 1);
            return reinterpret_cast< T* >(&ptr->data);
        }

        T* protect(const std::atomic< T* >& value, std::memory_order order = std::memory_order_seq_cst) {
            return value.load(order);
        }

        // TODO: store the retired node on guard
        void retire(T* ptr) {
            retire(&buffer_cast(ptr)->node);
        }

        void deallocate(T* ptr, size_t n) {
            assert(n == 1);
            deallocate(buffer_cast(ptr));
        }
    };
}
