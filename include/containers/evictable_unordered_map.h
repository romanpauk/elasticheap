//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#include <cassert>
#include <memory>
#include <optional>
#include <unordered_set>

namespace containers {
    namespace detail {
        template< typename Node > struct linked_list {
            using node_type = Node;
            using value_type = typename Node::value_type;

            struct iterator {
                iterator(const node_type* n) : node_(n) {}

                const value_type& operator*() { assert(node_); return node_->value; }
                const value_type* operator->() { assert(node_); return &node_->value; }

                bool operator == (const iterator& other) const { return node_ == other.node_; }
                bool operator != (const iterator& other) const { return node_ != other.node_; }

                iterator& operator++() { assert(node_); node_ = node_->next; return *this; }
                iterator operator++(int) { assert(node_); const node_type* n = node_; node_ = node_->next; return n; }

                const node_type* node_;
            };

            iterator begin() const {
                assert(!head_ || !head_->prev);
                return head_;
            }

            iterator end() const {
                assert(!tail_ || !tail_->next);
                return nullptr;
            }

            void push_front(const node_type& n) {
                if (head_) {
                    assert(!head_->prev);
                    assert(tail_);
                    assert(!tail_->next);
                    n.prev = nullptr;
                    n.next = head_;
                    head_->prev = &n;
                    head_ = &n;
                } else {
                    assert(!tail_);
                    head_ = tail_ = &n;
                    n.prev = n.next = nullptr;
                }
            }

            void push_back(const node_type& n) {
                if (!tail_) {
                    assert(!head_);
                    tail_ = head_ = &n;
                } else {
                    n.next = nullptr;
                    n.prev = tail_;
                    tail_->next = &n;
                    tail_ = &n;
                }
            }
            
            const node_type* erase(const node_type& n) {
                if (n.next) {
                    n.next->prev = n.prev;
                } else {
                    assert(tail_ == &n);
                    tail_ = n.prev;
                }

                if (n.prev) {
                    n.prev->next = n.next;
                } else {
                    assert(head_ == &n);
                    head_ = n.next;;
                }

                return n.next;
            }

            const node_type* head() const {
                assert(!head_ || !head_->prev);
                return head_;
            }

            const node_type* tail() const {
                assert(!tail_ || !tail_->next);
                return tail_;
            }

            void clear() { head_ = tail_ = nullptr; }

            bool empty() const { return head_ == nullptr; }

        private:
            const node_type* head_ = nullptr;
            const node_type* tail_ = nullptr;
        };

        template< typename T > struct lru_cache {
            struct node {
                using value_type = T;
                value_type value;
                mutable const node* next = nullptr;
                mutable const node* prev = nullptr;
            };

            using iterator = typename linked_list<node>::iterator;

            iterator evictable() const {
                return list_.tail();
            }

            iterator end() const { return list_.end(); }
            
            void erase(const node& n) { list_.erase(n); }

            void emplace(const node& n, bool inserted) {
                if (inserted) {
                    list_.push_front(n);
                } else if (&n != list_.head()) {
                    list_.erase(n);
                    list_.push_front(n);
                }
            }

            void touch(const node& n) {
                list_.erase(n);
                list_.push_front(n);
            }

        private:
            linked_list<node> list_;
        };

        template< typename T > struct lru_segmented_cache {
            struct node {
                using value_type = T;
                value_type value;
                mutable linked_list<node>* segment = nullptr;
                mutable const node* next = nullptr;
                mutable const node* prev = nullptr;
            };

            using iterator = typename linked_list<node>::iterator;

            iterator evictable() const {
                return segments_[0].empty() ? segments_[1].tail() : segments_[0].tail();
            }

            iterator end() const { return typename linked_list<node>::iterator(nullptr); }
            
            void erase(const node& n) { 
                n.segment->erase(n);
            }

            void emplace(const node& n, bool inserted) {
                if (inserted) {
                    n.segment = &segments_[0];
                    segments_[0].push_front(n);
                } else {
                    n.segment->erase(n);
                    n.segment = &segments_[1];
                    segments_[1].push_front(n);
                }
            }

            void touch(const node& n) {
                n.segment->erase(n);
                n.segment = &segments_[1];
                segments_[1].push_front(n);
            }

        private:
            linked_list<node> segments_[2];
        };

    #if __cpp_lib_generic_unordered_lookup != 201811L
        template< typename Node, typename Key, bool IsTriviallyDestructible = std::is_trivially_destructible_v<Key> > struct hashable_node;
        
        template< typename Node, typename Key> struct hashable_node<Node, Key, true> {
            hashable_node(const Key& key) {
                new (const_cast<Key*>(&node().value.first)) Key(key);
            }

            Node& node() { return *reinterpret_cast<Node*>(&storage_); }
        private:
            std::aligned_storage_t< sizeof(Node) > storage_;
        };

        template< typename Node, typename Key > struct hashable_node<Node, Key, false>
            : hashable_node<Node, Key, true> 
        {
            hashable_node(const Key& key): hashable_node<Node, Key, true>(key) {}
            ~hashable_node() { const_cast<Key*>(&this->node().value.first)->~Key(); }
        };
    #endif
    };

    template<
        typename Key,
        typename Value,
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Allocator = std::allocator< std::pair<const Key, Value > >,
        typename Cache = detail::lru_cache< std::pair< const Key, Value > >
    > class evictable_unordered_map {
    public:
        using cache_type = Cache;
        using node_type = typename Cache::node;
        using value_type = std::pair< const Key, Value >;
        using allocator_type = Allocator;

    private:    
        struct hash: Hash {
            size_t operator()(const node_type& n) const noexcept { return static_cast<const Hash&>(*this)(n.value.first); }
        #if __cpp_lib_generic_unordered_lookup == 201811L
            using is_transparent = void;
            size_t operator()(const Key& key) const noexcept { return static_cast<const Hash&>(*this)(key); }
        #endif
        };

        struct key_equal : KeyEqual {
            size_t operator()(const node_type& lhs, const node_type& rhs) const noexcept { return static_cast<const KeyEqual&>(*this)(lhs.value.first, rhs.value.first); }
        #if __cpp_lib_generic_unordered_lookup == 201811L
            using is_transparent = void;
            size_t operator()(const Key& lhs, const node_type& rhs) const noexcept { return static_cast<const KeyEqual&>(*this)(lhs, rhs.value.second); }
        #endif
        };

        using values_type = std::unordered_set< node_type, hash, key_equal,
            typename std::allocator_traits< Allocator >::template rebind_alloc< node_type > >;

        cache_type cache_;
        values_type values_;
        
    public:
        evictable_unordered_map() = default;
        evictable_unordered_map(Allocator allocator): values_(allocator) {}

        struct iterator {
            iterator(typename values_type::iterator it): it_(it) {}

            const std::pair<const Key, Value>& operator*() { return it_->value; }
            const std::pair<const Key, Value>* operator->() { return &it_->value; }

            bool operator == (const iterator& other) const { return it_ == other.it_; }
            bool operator != (const iterator& other) const { return it_ != other.it_; }

            iterator& operator++() { return ++it_; }
            iterator operator++(int) { typename values_type::iterator it = it_; ++it_; return it; }

        private:
            template< typename KeyT, typename ValueT, typename HashT, typename KeyEqualT, typename AllocatorT, typename CacheT>
            friend class evictable_unordered_map;

            typename values_type::iterator it_;
        };

        iterator begin() { return values_.begin(); }
        iterator end() { return values_.end(); }

        template<typename... Args> std::pair<iterator, bool> emplace(Args&&... args) {
            auto it = values_.emplace(node_type{{std::forward<Args>(args)...}});
            cache_.emplace(*it.first, it.second);
            return {it.first, it.second};
        }

        iterator find(const Key& key) {
        #if __cpp_lib_generic_unordered_lookup == 201811L
            auto it = values_.find(key);
        #else
            // This still needs to copy the key, but at least not the value.
            detail::hashable_node<node_type, Key> key_node(key);
            auto it = values_.find(key_node.node());
        #endif
            if (it != values_.end())
                cache_.touch(*it);
            return it;
        }

        Value& operator[](const Key& key) {
            auto it = find(key);
            if (it != end()) {
                return const_cast<Value&>(it->second); // TODO: const_cast
            }
            return const_cast<Value&>(emplace(key, Value()).first->second);
        }

        size_t erase(const Key& key) {
            auto it = find(key);
            if (it != values_.end()) {
                cache_.erase(it->second);
                values_.erase(it);
                return 1;
            }
            return 0;
        }

        iterator erase(const iterator& it) {
            cache_.erase(*it.it_);
            return values_.erase(it.it_);
        }

        void clear() {
            cache_.clear();
            values_.clear();
        }

        size_t size() const { return values_.size(); }
        bool empty() const { return values_.empty(); }

        void touch(const iterator& it) {
            assert(it != end());
            cache_.touch(*it.it_);
        }

        void touch(const Key& key) {
            auto it = find(key);
            if (it != end()) touch(it);
        }

        const Cache& cache() { return cache_; }

        iterator evictable() {
            auto it = cache_.evictable();
            if (it != cache_.end())
                return values_.find(*it.node_);
            return end();
        }
    };
}
