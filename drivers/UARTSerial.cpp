/* mbed Microcontroller Library
 * Copyright (c) 2006-2017 ARM Limited
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

#if (DEVICE_SERIAL && DEVICE_INTERRUPTIN)

#include <errno.h>
#include "UARTSerial.h"
#include "platform/mbed_poll.h"

#if MBED_CONF_RTOS_PRESENT
#include "rtos/Thread.h"
#else
#include "platform/mbed_wait_api.h"
#endif

extern "C" {
int tx_wait_count;
int tx_wake_count;
int rx_wait_count;
int rx_wake_count;
}

namespace mbed {

UARTSerial::UARTSerial(PinName tx, PinName rx, int baud) :
        SerialBase(tx, rx, baud),
        _blocking(true),
        _tx_irq_enabled(false),
        _rx_irq_enabled(true),
        _dcd_irq(NULL),
        _poll_wake_events(0)
{
    /* Attatch IRQ routines to the serial device. */
    SerialBase::attach(callback(this, &UARTSerial::rx_irq), RxIrq);
}

UARTSerial::~UARTSerial()
{
    delete _dcd_irq;
}

void UARTSerial::dcd_irq()
{
    wake(NULL, POLLHUP);
}

void UARTSerial::set_baud(int baud)
{
    SerialBase::baud(baud);
}

void UARTSerial::set_data_carrier_detect(PinName dcd_pin, bool active_high)
{
     delete _dcd_irq;
    _dcd_irq = NULL;

    if (dcd_pin != NC) {
        _dcd_irq = new InterruptIn(dcd_pin);
        if (active_high) {
            _dcd_irq->fall(callback(this, &UARTSerial::dcd_irq));
        } else {
            _dcd_irq->rise(callback(this, &UARTSerial::dcd_irq));
        }
    }
}

void UARTSerial::set_format(int bits, Parity parity, int stop_bits)
{
    core_util_critical_section_enter();
    SerialBase::format(bits, parity, stop_bits);
    core_util_critical_section_exit();
}

#if DEVICE_SERIAL_FC
void UARTSerial::set_flow_control(Flow type, PinName flow1, PinName flow2)
{
    core_util_critical_section_enter();
    SerialBase::set_flow_control(type, flow1, flow2);
    core_util_critical_section_exit();
}
#endif

int UARTSerial::close()
{
    /* Does not let us pass a file descriptor. So how to close ?
     * Also, does it make sense to close a device type file descriptor*/
    return 0;
}

int UARTSerial::isatty()
{
    return 1;

}

off_t UARTSerial::seek(off_t offset, int whence)
{
    /*XXX lseek can be done theoratically, but is it sane to mark positions on a dynamically growing/shrinking
     * buffer system (from an interrupt context) */
    return -ESPIPE;
}

int UARTSerial::sync()
{
    core_util_critical_section_enter();
    while (!_txbuf.empty()) {
        // We don't actually currently notify _cv_tx on empty, so use a timeout.
        // (And may as well still use the CV code we have anyway...)
        _cv_tx.wait_for(1);
    }
    core_util_critical_section_exit();

    return 0;
}

void UARTSerial::sigio(Callback<void()> func) {
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

ssize_t UARTSerial::write(const void* buffer, size_t length)
{
    size_t data_written = 0;
    const char *buf_ptr = static_cast<const char *>(buffer);

    if (length == 0) {
        return 0;
    }

    core_util_critical_section_enter();

    // Unlike read, we should write the whole thing if blocking. POSIX only
    // allows partial as a side-effect of signal handling; it normally tries to
    // write everything if blocking. Without signals we can always write all.
    while (data_written < length) {

        if (_txbuf.full()) {
            if (!_blocking) {
                break;
            }
            do {
                tx_wait_count++;
                _cv_tx.wait();
            } while (_txbuf.full());
        }

        while (data_written < length && !_txbuf.full()) {
            _txbuf.push(*buf_ptr++);
            data_written++;
        }

        if (!_tx_irq_enabled) {
            UARTSerial::tx_irq();                // only write to hardware in one place
            if (!_txbuf.empty()) {
                SerialBase::attach(callback(this, &UARTSerial::tx_irq), TxIrq);
                _tx_irq_enabled = true;
            }
        }
    }
    core_util_critical_section_exit();

    return data_written != 0 ? (ssize_t) data_written : (ssize_t) -EAGAIN;
}

ssize_t UARTSerial::read(void* buffer, size_t length)
{
    size_t data_read = 0;

    char *ptr = static_cast<char *>(buffer);

    if (length == 0) {
        return 0;
    }

    core_util_critical_section_enter();
    while (_rxbuf.empty()) {
        if (!_blocking) {
            core_util_critical_section_exit();
            return -EAGAIN;
        }
        rx_wait_count++;
        _cv_rx.wait();
    }

    while (data_read < length && !_rxbuf.empty()) {
        _rxbuf.pop(*ptr++);
        data_read++;
    }

    if (!_rx_irq_enabled) {
        UARTSerial::rx_irq();               // only read from hardware in one place
        if (!_rxbuf.full()) {
            SerialBase::attach(callback(this, &UARTSerial::rx_irq), RxIrq);
            _rx_irq_enabled = true;
        }
    }
    core_util_critical_section_exit();

    return data_read;
}

bool UARTSerial::hup() const
{
    return _dcd_irq && _dcd_irq->read() != 0;
}

void UARTSerial::wake(ConditionVariableCS *cv, short events)
{
    /* Unblock our own blocking read or write, depending on cv */
    if (cv) {
        cv->notify_all();
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

short UARTSerial::poll(short events) const {

    short revents = 0;
    /* Check the Circular Buffer if space available for writing out */

    if (!_rxbuf.empty()) {
        revents |= POLLIN;
    }

    /* POLLHUP and POLLOUT are mutually exclusive */
    if (hup()) {
        revents |= POLLHUP;
    } else if (!_txbuf.full()) {
        revents |= POLLOUT;
    }

    /*TODO Handle other event types */

    return revents;
}

short UARTSerial::poll_with_wake(short events, bool wake)
{
    short revents = poll(events);
    if (wake && !(revents & events)) {
        _poll_wake_events |= events;
    }
    return revents;
}

void UARTSerial::lock()
{
    // This is the override for SerialBase.
    // No lock required as we only use SerialBase from interrupt or from
    // inside our own critical section.
}

void UARTSerial::unlock()
{
    // This is the override for SerialBase.
}

void UARTSerial::rx_irq(void)
{
    bool was_empty = _rxbuf.empty();

    /* Fill in the receive buffer if the peripheral is readable
     * and receive buffer is not full. */
    while (!_rxbuf.full() && SerialBase::readable()) {
        char data = SerialBase::_base_getc();
        _rxbuf.push(data);
    }

    if (_rx_irq_enabled && _rxbuf.full()) {
        SerialBase::attach(NULL, RxIrq);
        _rx_irq_enabled = false;
    }

    /* Report the File handler that data is ready to be read from the buffer. */
    if (was_empty && !_rxbuf.empty()) {
        rx_wake_count++;
        wake(&_cv_rx, POLLIN);
    }
}

// Also called from write to start transfer
void UARTSerial::tx_irq(void)
{
    bool was_full = _txbuf.full();

    /* Write to the peripheral if there is something to write
     * and if the peripheral is available to write. */
    while (!_txbuf.empty() && SerialBase::writeable()) {
        char data;
        _txbuf.pop(data);
        SerialBase::_base_putc(data);
    }

    if (_tx_irq_enabled && _txbuf.empty()) {
        SerialBase::attach(NULL, TxIrq);
        _tx_irq_enabled = false;
    }

    /* Report the File handler that data can be written to peripheral. */
    if (was_full && !_txbuf.full() && !hup()) {
        tx_wake_count++;
        wake(&_cv_tx, POLLOUT);
    }
}
} //namespace mbed

#endif //(DEVICE_SERIAL && DEVICE_INTERRUPTIN)
