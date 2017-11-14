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
#ifndef CONDITIONVARIABLECS_H
#define CONDITIONVARIABLECS_H

#include <stdint.h>
#include "cmsis_os.h"
#include "rtos/Mutex.h"

#include "platform/NonCopyable.h"


namespace mbed {
/** \addtogroup mbed */
/** @{*/

struct Waiter;

/** This class provides a safe way to wait for or send notifications of condition changes
 *
 * This class is used in conjunction with a critical section to safely wait for or
 * notify waiters of condition changes to a resource accessible by multiple
 * threads and interrupts.
 *
 * This provides similar functionality to rtos::ConditionVariable, except that it
 * is used when the notifications occur from interrupt, which means the resource
 * must be protected by a critical section (disabling IRQs) instead of a mutex,
 * and it is usable in non-RTOS systems.
 *
 * # Defined behavior
 * - All threads waiting on the condition variable wake when
 *   ConditionVariableCS::notify_all is called.
 * - If one or more threads are waiting on the condition variable at least
 *   one of them wakes when ConditionVariableCS::notify_one is called.
 *
 * # Undefined behavior
 * - The thread which is unblocked on ConditionVariableCS::notify_one is
 *   undefined if there are multiple waiters.
 * - The order which in which waiting threads enter the critical section after
 *   ConditionVariableCS::notify_all is called is undefined.
 * - When ConditionVariableCS::notify_one or ConditionVariableCS::notify_all is
 *   called and there are one or more waiters and one or more threads attempting
 *   to enter a critical section the order in which the critical section is
 *   entered is undefined.
 * - The behavior of ConditionVariableCS::wait and ConditionVariableCS::wait_for
 *   is undefined if core_util_critical_section_enter() has been entered more
 *   than once by the calling thread.
 * - Spurious notifications (not triggered by the application) can occur
 *   and it is not defined when these occur.
 * - RTOS thread flags (aka signals) may be changed by a call to
 *   ConditionVariableCS::wait.
 *
 * @note Synchronization level: Thread safe
 *
 * Example:
 * @code
 * #include "mbed.h"
 *
 * ConditionVariableCS cond;
 * Ticker tick;
 *
 * // These variables are protected by critical section
 * uint32_t count = 0;
 * bool done = false;
 *
 * void counter()
 * {
 *     if (++count == 6) {
 *         done = true;
 *     }
 *     cond.notify_all();
 * }
 *
 * int main() {
 *     tick.attach(counter, 1);
 *
 *     printf("Worker: Starting\r\n");
 *     core_util_critical_section_enter();
 *     while (!done) {
 *         uint32_t c = count;
 *
 *         core_util_critical_section_exit();
 *         printf("Worker: Count %lu\r\n", c);
 *         core_util_critical_section_enter();
 *
 *         cond.wait();
 *     }
 *
 *     core_util_critical_section_exit();
 *     printf("Worker: Exiting\r\n");
 * }
 *  * @endcode
 */
class ConditionVariableCS : private NonCopyable<ConditionVariableCS> {
public:

    /** Create and Initialize a ConditionVariableCS object */
    ConditionVariableCS();

    /** Wait for a notification
     *
     * Wait until a notification occurs.
     *
     * @note - The thread calling this function must be be in a critical
     * section and it must be locked exactly once
     * @note - Spurious notifications can occur so the caller of this API
     * should check to make sure the condition they are waiting on has
     * been met
     * @note - in an RTOS, this may be implemented using a thread flag
     * (osThreadFlagsXXX/rtos::Thread::signal_XXX), which means a call to wait
     * may modify flag state.
     *
     * Example:
     * @code
     * core_util_critical_section_enter();
     * while (!condition_met) {
     *     cond.wait();
     * }
     *
     * function_to_handle_condition();
     *
     * core_util_critical_section_exit();
     * @endcode
     */
    void wait();

    /** Wait for a notification or timeout
     *
     * @param   millisec  timeout value or 0xFFFFFFFF in case of no time-out.
     * @return  true if a timeout occurred, false otherwise.
     *
     * @note - The thread calling this function must be in a critical section
     * and it must be locked exactly once
     * @note - Spurious notifications can occur so the caller of this API
     * should check to make sure the condition they are waiting on has
     * been met
     * @note - in an RTOS, this may be implemented using a thread flag
     * (osThreadFlagsXXX/rtos::Thread::signal_XXX), which means a call to wait
     * may modify flag state.

     * Example:
     * @code
     * Timer timer;
     * timer.start();
     *
     * bool timed_out = false;
     * uint32_t time_left = TIMEOUT;
     * core_util_critical_section_enter();
     * while (!condition_met && !timed_out) {
     *     timed_out = cond.wait_for(time_left);
     *     uint32_t elapsed = timer.read_ms();
     *     time_left = elapsed > TIMEOUT ? 0 : TIMEOUT - elapsed;
     * }
     *
     * if (condition_met) {
     *     function_to_handle_condition();
     * }
     *
     * core_util_critical_section_exit();
     * @endcode
     */
    bool wait_for(uint32_t millisec);
    
    /* TODO - lift documentation from ConditionVariable::wait_until */
    bool wait_until(uint64_t end_time);

    static uint64_t current_time();

    /** Notify one waiter on this condition variable that a condition changed.
     *
     * @note - This may be called from interrupt or thread, either in a critical
     * section or not.
     *
     */
    void notify_one();

    /** Notify all waiters on this condition variable that a condition changed.
     *
     * @note - This may be called from interrupt or thread, either in a critical
     * section or not.
     */
    void notify_all();

    ~ConditionVariableCS();

private:
#if MBED_CONF_RTOS_PRESENT
    void _add_wait_list(Waiter * waiter);
    void _remove_wait_list(Waiter * waiter);
    Waiter *_wait_list; // protected by critical section
#else
    static void _timeout(bool *flag);
    bool _notified; // protected by critical section
#endif
};

}
#endif

/** @}*/
