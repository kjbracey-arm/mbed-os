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
#include "platform/ConditionVariableCS.h"
#if MBED_CONF_RTOS_PRESENT
#include "cmsis_os2.h"
#include "rtos/Semaphore.h"
#include "rtos/Thread.h"
#endif

#include "mbed_critical.h"
#include "mbed_error.h"
#include "mbed_assert.h"

namespace mbed {

#if MBED_CONF_RTOS_PRESENT

#define THREAD_FLAG_UNBLOCK (1 << 30)

struct Waiter {
    Waiter();
    osThreadId_t tid;
    Waiter *prev;
    Waiter *next;
    bool in_list;
};

Waiter::Waiter(): tid(rtos::Thread::gettid()), prev(NULL), next(NULL), in_list(false)
{
    // No initialization to do
}

ConditionVariableCS::ConditionVariableCS(): _wait_list(NULL)
{
    // No initialization to do
}
#else
ConditionVariableCS::ConditionVariableCS()
{
    // No initialization to do
}
#endif

uint64_t ConditionVariableCS::current_time()
{
#if MBED_CONF_RTOS_PRESENT
    /* TODO - should be rtos::Kernel::GetMillisecondTicks() */
    return osKernelGetTickCount();
#elif DEVICE_LOWPOWERTIMER
    /* No wrap problems for 500,000 years */
    return ticker_read_us(get_lp_ticker_data()) / 1000;
#else
    return ticker_read_us(get_us_ticker_data()) / 1000;
#endif
}

bool ConditionVariableCS::wait_for(uint32_t millisec)
{
    MBED_ASSERT(!core_util_are_interrupts_enabled());

#if MBED_CONF_RTOS_PRESENT
    Waiter current_thread;

    _add_wait_list(&current_thread);

    core_util_critical_section_exit();

    MBED_ASSERT(core_util_are_interrupts_enabled());
    uint32_t ret = osThreadFlagsWait(THREAD_FLAG_UNBLOCK, osFlagsWaitAny, millisec);
    bool timeout;
    if (ret == osFlagsErrorTimeout) {
        MBED_ASSERT(millisec != osWaitForever);
        timeout = true;
    } else if (ret == osFlagsErrorResource && millisec == 0) {
        timeout = true;
    } else {
        MBED_ASSERT(!(ret & osFlagsError) && (ret & THREAD_FLAG_UNBLOCK));
        timeout = false;
    }

    core_util_critical_section_enter();

    if (current_thread.in_list) {
        _remove_wait_list(&current_thread);
    }
    return timeout;
#else
#if DEVICE_LOWPOWERTIMER
    LowPowerTimeout timeout;
#else
    Timeout timeout;
#endif
    bool timed_out = millisec == 0;
    _notified = false;
    if (millisec > 0 && millisec < 0xFFFFFFFFu) {
        timeout.attach_us(callback(this, &ConditionVariableCS::_timeout, &timed_out), (us_timestamp_t) millisec * 1000);
    }
    do {
        core_util_critical_section_exit();

        MBED_ASSERT(core_util_are_interrupts_enabled());
        // ISB ensures interrupts have time to run between IRQ enable and disable
        __ISB();
        // If we aren't just polling, wait for an event, which must occur
        // if someone notifies us (as side-effect of the IRQ that gave them
        // control), or if the timeout expires (generating an IRQ)
        if (millisec != 0) {
            __WFE();
        }

        core_util_critical_section_enter();
        if (_notified) {
            return false;
        }
    } while (!timed_out);
    return true;
#endif
}

void ConditionVariableCS::wait()
{
    wait_for(0xFFFFFFFFu);
}

bool ConditionVariableCS::wait_until(uint64_t end_time)
{
    uint64_t now = current_time();
    if (now >= end_time) {
        return wait_for(0);
    } else {
        return wait_for(end_time - now);
    }
}

void ConditionVariableCS::notify_one()
{
#if MBED_CONF_RTOS_PRESENT
    core_util_critical_section_enter();
    if (_wait_list != NULL) {
        _remove_wait_list(_wait_list);
        core_util_critical_section_exit();
        uint32_t flags = osThreadFlagsSet(_wait_list->tid, THREAD_FLAG_UNBLOCK);
        MBED_ASSERT(!(flags & osFlagsError));
    } else {
        core_util_critical_section_exit();
    }
#else
    core_util_critical_section_enter();
    _notified = true;
    // Would need SEV if we supported multicore
    core_util_critical_section_exit();
#endif
}

void ConditionVariableCS::notify_all()
{
#if MBED_CONF_RTOS_PRESENT
    // Unhook all in one go in critical section, then notify outside
    core_util_critical_section_enter();
    Waiter *waiter = _wait_list;
    _wait_list = NULL;
    core_util_critical_section_exit();

    while (waiter != NULL) {
        Waiter *next = waiter->next;
        waiter->next = NULL;
        waiter->prev = NULL;
        waiter->in_list = false;
        uint32_t flags = osThreadFlagsSet(waiter->tid, THREAD_FLAG_UNBLOCK);
        MBED_ASSERT(!(flags & osFlagsError));
        waiter = next;
    }
#else
    notify_one();
#endif
}

#if MBED_CONF_RTOS_PRESENT
void ConditionVariableCS::_add_wait_list(Waiter *waiter)
{
    if (NULL == _wait_list) {
        // Nothing in the list so add it directly.
        // Update prev and next pointer to reference self
        _wait_list = waiter;
        waiter->next = waiter;
        waiter->prev = waiter;
    } else {
        // Add after the last element
        Waiter *first = _wait_list;
        Waiter *last = _wait_list->prev;

        // Update new entry
        waiter->next = first;
        waiter->prev = last;

        // Insert into the list
        first->prev = waiter;
        last->next = waiter;
    }
    waiter->in_list = true;
}

void ConditionVariableCS::_remove_wait_list(Waiter *waiter)
{
    Waiter *prev = waiter->prev;
    Waiter *next = waiter->next;

    // Remove from list
    prev->next = waiter->next;
    next->prev = waiter->prev;
    _wait_list = waiter->next;

    if (_wait_list == waiter) {
        // This was the last element in the list
        _wait_list = NULL;
    }

    // Invalidate pointers
    waiter->next = NULL;
    waiter->prev = NULL;
    waiter->in_list = false;
}
#else
void ConditionVariableCS::_timeout(bool *flag)
{
    *flag = true;
}
#endif

ConditionVariableCS::~ConditionVariableCS()
{
#if MBED_CONF_RTOS_PRESENT
    MBED_ASSERT(NULL == _wait_list);
#endif
}

}
