//
// This file is part of containers project <https://github.com/romanpauk/containers>
//
// See LICENSE for license and copyright information
// SPDX-License-Identifier: AGPL-3.0-or-later
//

#pragma once

#include <memory>
#include <mutex>

template< typename T > struct factory {
    static T& get() {
        std::lock_guard lock(_mutex);
        if (!_instance) _instance.reset(new T);
        return *_instance;
    }

    static void reset() { _instance.reset(); }

    static std::mutex _mutex;
    static std::unique_ptr< T > _instance;
};

template< typename T > std::mutex factory< T >::_mutex;
template< typename T > std::unique_ptr< T > factory< T >::_instance;