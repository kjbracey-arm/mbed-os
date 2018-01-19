/* mbed Microcontroller Library
 * Copyright (c) 2017 ARM Limited
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
#include "mbed_poll.h"
#include "mbed_critical.h"
#include "FileHandle.h"
#include "Timer.h"
#include "ConditionVariableCS.h"
#ifdef MBED_CONF_RTOS_PRESENT
#include "rtos/Thread.h"
#endif

extern "C" {
int poll_wait_count;
int poll_wake_count;
}

namespace mbed {

// This is small enough (1 word) to make efforts to use SingletonPtr or
// similar not seem worthwhile. We need to have the CV anyway if poll()
// or any poll_with_wake()-capable FileHandle is ever used, and we expect
// UARTSerial to be commonly used for trace.
static ConditionVariableCS wake_cv;

// timeout -1 forever, or milliseconds
int poll(pollfh fhs[], unsigned nfhs, int timeout)
{
    // Don't bother reading time if finite timeout not specified
    uint64_t finish_time = timeout > 0 ? wake_cv.current_time() + timeout : 0;

    int count = 0;
    // poll_with_wake is always called from critical section
    // poll is never called from critical section, in case they are using a mutex
    // all_handles_support_wake is true iff we are in a CS
    bool all_handles_support_wake = true;
    core_util_critical_section_enter();
    for (;;) {
        /* Scan the file handles */
        for (unsigned n = 0; n < nfhs; n++) {
            FileHandle *fh = fhs[n].fh;
            short mask = fhs[n].events | POLLERR | POLLHUP | POLLNVAL;
            if (fh) {
                /* Try the new poll_with_wake, if everyone is supporting it */
                if (all_handles_support_wake) {
                    /* Still in critical section */
                    fhs[n].revents = fh->poll_with_wake(mask, count == 0 && timeout != 0) & mask;
                    if (fhs[n].revents & POLLNVAL) {
                        /* Okay, this file doesn't support it - fall back to timed polling and leave CS */
                        all_handles_support_wake = false;
                        core_util_critical_section_exit();
                    }
                }
                if (!all_handles_support_wake) {
                    fhs[n].revents = fh->poll(mask) & mask;
                }
            } else {
                fhs[n].revents = POLLNVAL;
            }
            if (fhs[n].revents) {
                count++;
            }
        }

        /* Stop as soon as we have positive count, or if we don't want to block */
        if (count > 0 || timeout == 0) {
            break;
        }

        /* Now we block until something happens (or may have happened) */
        if (all_handles_support_wake) {
            // In critical section. Use ConditionVariableCS to wait.
            poll_wait_count++;
            bool timed_out;
            if (timeout > 0) {
                timed_out = wake_cv.wait_until(finish_time);
            } else {
                wake_cv.wait();
                timed_out = false;
            }
            if (timed_out) {
                /* Wait timed out - rescan for final result */
                timeout = 0;
                continue;
            }
        } else {
            // Not in critical section. Backwards compatible 1ms polling for
            // FileHandles that only support the original poll().
            if (timeout > 0 && wake_cv.current_time() >= finish_time) {
                break;
            }
#ifdef MBED_CONF_RTOS_PRESENT
            rtos::Thread::wait(1);
#else
            wait_ms(1);
#endif
        }
    }
    if (all_handles_support_wake) {
        core_util_critical_section_exit();
    }
    return count;
}

// We ask users to give us their FileHandle and events, but don't
// actually currently use this information.
// As there is a single CV for the entire system, there
// would be lots of spurious wakeups if we had lots of threads.
// We assume that as we're targeting small embedded systems, this
// won't be the case - we're optimising for RAM, so we'd rather
// minimise data structures anyway. The fact we pass the events
// to poll_with_wait allows file handles to somewhat filter
// spurious wakeups - if implemented well they will only call
// wake_poll once after each poll_with_wake, so we won't get
// continuous spurious wakeups from FileHandles that aren't
// involved in a blocking poll.
void FileHandle::wake_poll(short /*events*/)
{
    core_util_critical_section_enter();
    wake_cv.notify_all();
    poll_wake_count++;
    core_util_critical_section_exit();
}

} // namespace mbed
