//
// This file is part of elasticheap project <https://github.com/romanpauk/elasticheap>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: MIT
//

#pragma once

#include <elasticheap/detail/bitset.h>

#include <cassert>

namespace elasticheap::detail {

template< typename T, std::size_t Capacity > struct bitset_heap {
    bitset_heap() {
        bitmap_.clear();
    }

    void push(T value) {
        assert(value < Capacity);
        assert(!bitmap_.get(value));
        bitmap_.set(value);
        ++size_;
        if (min_ > value) min_ = value;
        if (max_ < value) max_ = value;
    }

    bool empty() const {
        return size_ == 0;
    }

    T pop() {
        assert(!empty());
        T min = min_;
        assert(bitmap_.get(min));
        bitmap_.clear(min);
        --size_;
        for(std::size_t i = min + 1; i <= max_; ++i) {
            if (bitmap_.get(i)) {
                min_ = i;
                return min;
            }
        }

        min_ = Capacity;
        max_ = 0;

        return min;
    }

    const T& top() {
        assert(!empty());
        return min_;
    }

    std::size_t size() const { return size_; }

private:
    std::size_t size_ = 0;
    T min_ = Capacity;
    T max_ = 0;
    detail::bitset<Capacity> bitmap_;
};

}