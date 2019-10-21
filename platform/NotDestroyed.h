/* mbed Microcontroller Library
 * Copyright (c) 2006-2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef MBED_NOTDESTROYED_H
#define MBED_NOTDESTROYED_H

#include "platform/NonCopyable.h"

namespace mbed {

/** \ingroup mbed-os-public */
/** \addtogroup platform-public-api */
/** @{*/
/**
 * \defgroup platform_NotDestroyed NotDestroyed class
 * @{
 */

/** Utility class for preventing destruction of an object
 *
 * @note Synchronization level: Thread safe
 *
 * This class bypasses the destructor of the wrapped object,
 * while retaining its constructor. The wrapper provides
 * access via '*', '->' and @ref value(),
 *
 * NotDestroyed would normally be used for a static object,
 * to avoid linking unneeded destructor code into the image.
 * Without this wrapper, compilers can include destructor
 * code and attempt to register it with `__cxa_atexit`, but
 * this is ineffective - Mbed OS does not call registered
 * finalisation at any time; it has no shutdown sequence.
 *
 * @note @parblock If the wrapped object's constructor is not trivial
 * zero initialization or constant initialization, and it is
 * a global static object, then the object will be pulled into
 * the image for construction even if not referenced. To avoid
 * this, @ref SingletonPtr should be used to wrap the object
 * instead. That adds construct-on-first-use semantics, but
 * also a little run-time overhead.
 *
 * NotDestroyed is appropriate when using classes that have
 * constexpr constructors but non-constexpr destructors,
 * such as std::mutex, as global variables.
 *
 * It is also appropriate for function-wrapped on-first-use
 * constructs such as:
 *
 *     EMAC &EMAC::get_default_instance()
 *     {
 *         NotDestroyed<MyEMAC> emac;
 *         return emac.value();
 *     }
 *
 * @endparblock
 */
template<typename T>
union NotDestroyed {
private:
    char dummy;
    T val;

public:
    /** Construct the object
     *
     * @param args arguments for the wrapped object's constructor
     */
    template <typename... Args>
    constexpr NotDestroyed(Args&&... args) : val(std::forward<Args>(args)...) { }

    /** Destroy the object - do nothing
     */
    ~NotDestroyed() { }

    /* Disable copy construction */
    NotDestroyed(const NotDestroyed &) = delete;

    /* Disable copy assignment */
    void operator=(const NotDestroyed &) = delete;

    /** Get a pointer to the wrapped object
     *
     * @return a pointer to the object
     */
    constexpr const T *operator->() const
    {
        return &val;
    }

    /** Get a pointer to the wrapped object
     *
     * @return a pointer to the object
     */
    constexpr T *operator->()
    {
        return &val;
    }

    /** Get a reference to the wrapped object
     *
     * @return a reference to the object
     */
    constexpr const T &operator*() const
    {
        return val;
    }

    /** Get a reference to the wrapped object
     *
     * @return a reference to the object
     */
    constexpr T &operator*()
    {
        return val;
    }

    /** Get the value of the wrapped object
     *
     * @return a reference to the object
     */
    constexpr const T &value() const &
    {
        return val;
    }

    /** Get the value of the wrapped object
     *
     * @return a reference to the object
     */
    constexpr const T &&value() const &&
    {
        return val;
    }

    /** Get the value of the wrapped object
     *
     * @return a reference to the object
     */
    constexpr T &value() &
    {
        return val;
    }

    /** Get the value of the wrapped object
     *
     * @return a reference to the object
     */
    constexpr T &&value() &&
    {
        return val;
    }
};

/**@}*/

/**@}*/

} // namespace mbed

#endif /* MBED_NOTDESTROYED_H_ */
