/* mbed Microcontroller Library
 * Copyright (c) 2006-2016 ARM Limited
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
#include "FileHandle.h"
#include "platform/mbed_retarget.h"
#include "platform/mbed_critical.h"

namespace mbed {

off_t FileHandle::size()
{
    /* remember our current position */
    off_t off = seek(0, SEEK_CUR);
    if (off < 0) {
        return off;
    }
    /* seek to the end to get the file length */
    off_t size = seek(0, SEEK_END);
    /* return to our old position */
    seek(off, SEEK_SET);
    return size;
}

extern "C" {
int tx_wait_count;
int tx_wake_count;
int rx_wait_count;
int rx_wake_count;
}

ssize_t FileHandleDeviceWakeHelper::read(void *buffer, size_t size)
{
    ssize_t amount_read;

    for (;;) {
        amount_read = read_nonblocking(buffer, size);
        if (amount_read == -EAGAIN && is_blocking()) {
            rx_wait_count++;
            _cv_rx.wait();
        } else {
            // devices return as soon as they have anything
            break;
        }
    }

    return amount_read;
}

ssize_t FileHandleDeviceWakeHelper::write(const void *buffer, size_t size)
{
    ssize_t amount_written = 0;
    char *ptr = static_cast<const char *>(buffer);

    for (;;) {
        ssize_t n = write_nonblocking(ptr, size - amount_written);
        if (n >= 0) {
            MBED_ASSERT(n <= size - amount_written);
            ptr += n;
            amount_written += n;
            if (amount_written >= size || !is_stream()) {
                break;
            }
        } else if (n == -EAGAIN && is_blocking()) {
            tx_wait_count++;
            _cv_tx.wait();
        } else  {
            /* other error - forget total, return error */
            amount_written = n;
            break;
        }
    }

    return amount_written;
}

short FileHandleDeviceWakeHelper::poll_with_wake(short events, bool wake)
{
    short revents = poll(events);
    if (wake && !(revents & events)) {
        _poll_wake_events |= events;
    }
    return revents;
}

void FileHandleDeviceWakeHelper::wake(short events)
{
    /* Unblock our own blocking read or write */
    if (events & (POLLIN|POLLERR)) {
        rx_wake_count++;
        _cv_rx.notify_all();
    }
    if (events & (POLLOUT|POLLHUP|POLLERR)) {
        tx_wake_count++;
        _cv_tx.notify_all();
    }
    /* Unblock poll, if it's in use */
    if (_poll_wake_events & events) {
        _poll_wake_events &= ~events;
        wake_poll(events);
    }
    /* Raise SIGIO */
    if (_sigio_cb) {
        _sigio_cb();
    }
}

void FileHandleDeviceWakeHelper::sigio(Callback<void()> func) {
    core_util_critical_section_enter();
    _sigio_cb = func;
    if (_sigio_cb) {
        short current_events = poll(0x7FFF);
        if (current_events) {
            _sigio_cb();
        }
    }
    core_util_critical_section_exit();
}

std::FILE *fdopen(FileHandle *fh, const char *mode)
{
    return mbed_fdopen(fh, mode);
}

} // namespace mbed
