/* mbed Microcontroller Library
 * Copyright (c) 2017-2017 ARM Limited
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "rtos/ConditionVariable.h"
#include "rtos/Semaphore.h"
#include "rtos/Thread.h"

#include "ns_list.h"

#include "mbed_error.h"
#include "mbed_assert.h"

namespace rtos {

#define RESUME_SIGNAL      (1 << 15)

struct Waiter {
    Waiter();
    ns_list_link_t link;
    Semaphore sem;
    bool in_list;
};

NS_STATIC_ASSERT(offsetof(Waiter, link) == 0,
    "Waiter::link must be first, because ConditionVariable::_wait_list uses NS_LIST_HEAD_INCOMPLETE")

Waiter::Waiter(): sem(0), in_list(false)
{
    ns_list_link_init(this, link);
}

ConditionVariable::ConditionVariable(Mutex &mutex): _mutex(mutex)
{
    ns_list_init(&_wait_list);
}

void ConditionVariable::wait()
{
    wait_for(osWaitForever);
}

bool ConditionVariable::wait_for(uint32_t millisec)
{
    Waiter current_thread;
    MBED_ASSERT(_mutex.get_owner() == Thread::gettid());
    MBED_ASSERT(_mutex._count == 1);
    ns_list_add_to_end(&_wait_list, &current_thread);
    current_thread.in_list = true;

    _mutex.unlock();

    int32_t sem_count = current_thread.sem.wait(millisec);
    bool timeout = (sem_count > 0) ? false : true;

    _mutex.lock();

    if (current_thread.in_list) {
        ns_list_remove(&_wait_list, &current_thread);
    }

    return timeout;
}

void ConditionVariable::notify_one()
{
    MBED_ASSERT(_mutex.get_owner() == Thread::gettid());
    Waiter *waiter = ns_list_get_first(&_wait_list);
    if (waiter) {
        ns_list_remove(&_wait_list, waiter);
        waiter->in_list = false;
        waiter->sem.release();
    }
}

void ConditionVariable::notify_all()
{
    MBED_ASSERT(_mutex.get_owner() == Thread::gettid());
    ns_list_foreach_safe(Waiter, waiter, &_wait_list) {
        ns_list_remove(&_wait_list, waiter);
        waiter->in_list = false;
        waiter->sem.release();
   }
}

ConditionVariable::~ConditionVariable()
{
    MBED_ASSERT(ns_list_is_empty(&_wait_list));
}

}
