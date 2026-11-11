/*
 * This file is part of Betaflight.
 *
 * GD32F30x USART driver (IRQ based; DMA hooks are present but no UART DMA
 * resources are assigned on this platform yet).
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_UART

#include "build/atomic.h"

#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"

#include "platform/rcc.h"

static bool uartUsesSmartAudio(const uartPort_t *uartPort)
{
    const portOptions_e options = SERIAL_BIDIR | SERIAL_PULL_SMARTAUDIO;
    return (uartPort->port.options & options) == options;
}

static IO_t uartTxIo(uartPort_t *uartPort)
{
    uartDevice_t *uart = container_of(uartPort, uartDevice_t, port);
    return IOGetByTag(uart->tx.pin);
}

static void uartSmartAudioReceive(uartPort_t *uartPort)
{
    const uint32_t usart = (uint32_t)uartPort->USARTx;

    // Release the push-pull output so the VTX can drive the same wire.
    IOConfigGPIO(uartTxIo(uartPort), IOCFG_IPD);
    usart_transmit_config(usart, USART_TRANSMIT_DISABLE);
    usart_interrupt_disable(usart, USART_INT_TC);
    if (uartPort->port.mode & MODE_RX) {
        usart_receive_config(usart, USART_RECEIVE_ENABLE);
    }
}

void uartReconfigure(uartPort_t *uartPort)
{
    const uint32_t usart = (uint32_t)uartPort->USARTx;
    const bool smartAudio = uartUsesSmartAudio(uartPort);

    usart_disable(usart);
    usart_deinit(usart);

    usart_baudrate_set(usart, uartPort->port.baudRate);

    /* With parity, the word length has to include the parity bit. */
    usart_word_length_set(usart, (uartPort->port.options & SERIAL_PARITY_EVEN) ? USART_WL_9BIT : USART_WL_8BIT);
    usart_stop_bit_set(usart, (uartPort->port.options & SERIAL_STOPBITS_2) ? USART_STB_2BIT : USART_STB_1BIT);
    usart_parity_config(usart, (uartPort->port.options & SERIAL_PARITY_EVEN) ? USART_PM_EVEN : USART_PM_NONE);

    usart_hardware_flow_rts_config(usart, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(usart, USART_CTS_DISABLE);

    if (smartAudio) {
        uartSmartAudioReceive(uartPort);
    } else {
        usart_receive_config(usart, (uartPort->port.mode & MODE_RX) ? USART_RECEIVE_ENABLE : USART_RECEIVE_DISABLE);
        usart_transmit_config(usart, (uartPort->port.mode & MODE_TX) ? USART_TRANSMIT_ENABLE : USART_TRANSMIT_DISABLE);
    }

    uartConfigureExternalPinInversion(uartPort);

    if (uartPort->port.options & SERIAL_BIDIR) {
        usart_halfduplex_enable(usart);
    } else {
        usart_halfduplex_disable(usart);
    }

    usart_enable(usart);

    /*
     * GD32F30x is an F1-style pin mux: the generic pin setup configures both
     * TX and RX as AF push-pull.  RX must be an input (floating or pulled up)
     * or the receiver never sees a proper idle level.
     */
    if (!smartAudio) {
        uartDevice_t *uart = container_of(uartPort, uartDevice_t, port);
        IO_t rxIO = IOGetByTag(uart->rx.pin);
        if (rxIO) {
            IOConfigGPIO(rxIO, IOCFG_IPU);
        }
    }

    if (uartPort->port.mode & MODE_RX) {
        usart_interrupt_flag_clear(usart, USART_INT_FLAG_RBNE);
        usart_interrupt_enable(usart, USART_INT_RBNE);
        usart_interrupt_enable(usart, USART_INT_IDLE);
    }

    if (uartPort->port.mode & MODE_TX) {
        usart_interrupt_enable(usart, USART_INT_TBE);
        if (!smartAudio) {
            usart_interrupt_enable(usart, USART_INT_TC);
        }
    }
}

bool checkUsartTxOutput(uartPort_t *s)
{
    uartDevice_t *uart = container_of(s, uartDevice_t, port);
    IO_t txIO = IOGetByTag(uart->tx.pin);
    if (uart->txPinState == TX_PIN_MONITOR && txIO) {
        if (!IORead(txIO)) {
            return false;
        }
        uart->txPinState = TX_PIN_ACTIVE;
        IOConfigGPIOAF(txIO, IOCFG_AF_PP, uart->hardware->af);
        usart_transmit_config((uint32_t)s->USARTx, USART_TRANSMIT_ENABLE);
    }
    return true;
}

void uartTxMonitor(uartPort_t *s)
{
    uartDevice_t *uart = container_of(s, uartDevice_t, port);

    if (uart->txPinState == TX_PIN_ACTIVE) {
        const uint32_t usart = (uint32_t)s->USARTx;
        usart_transmit_config(usart, USART_TRANSMIT_DISABLE);
        uart->txPinState = TX_PIN_MONITOR;
        IOConfigGPIO(IOGetByTag(uart->tx.pin), IOCFG_IPU);
    }
}

void uartIrqHandler(uartPort_t *s)
{
    const uint32_t usart = (uint32_t)s->USARTx;
    const bool smartAudio = uartUsesSmartAudio(s);

    if (usart_interrupt_flag_get(usart, USART_INT_FLAG_RBNE) == SET) {
        // A SmartAudio line held low while idle can be seen as a break.
        const bool framingError = smartAudio && usart_flag_get(usart, USART_FLAG_FERR) == SET;
        const uint8_t data = (uint8_t)usart_data_receive(usart);

        if (!framingError) {
            if (s->port.rxCallback) {
                s->port.rxCallback(data, s->port.rxCallbackData);
            } else {
                s->port.rxBuffer[s->port.rxBufferHead] = data;
                s->port.rxBufferHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
            }
        }
    }

    if (usart_interrupt_flag_get(usart, USART_INT_FLAG_TC) == SET) {
        if (s->port.txBufferHead == s->port.txBufferTail) {
            if (smartAudio) {
                uartSmartAudioReceive(s);
            } else {
                uartTxMonitor(s);
            }
        }
        usart_interrupt_flag_clear(usart, USART_INT_FLAG_TC);
    }

    if (usart_interrupt_flag_get(usart, USART_INT_FLAG_TBE) == SET) {
        if (s->port.txBufferTail != s->port.txBufferHead) {
            if (smartAudio && !(USART_CTL0(usart) & USART_CTL0_TEN)) {
                usart_receive_config(usart, USART_RECEIVE_DISABLE);
                IOConfigGPIO(uartTxIo(s), IOCFG_AF_PP);
                usart_transmit_config(usart, USART_TRANSMIT_ENABLE);
                usart_interrupt_enable(usart, USART_INT_TC);
            }
            usart_data_transmit(usart, s->port.txBuffer[s->port.txBufferTail]);
            s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
        } else {
            usart_interrupt_disable(usart, USART_INT_TBE);
        }
    }

    if (usart_interrupt_flag_get(usart, USART_INT_FLAG_ERR_ORERR) == SET) {
        usart_interrupt_flag_clear(usart, USART_INT_FLAG_ERR_ORERR);
    }

    if (usart_interrupt_flag_get(usart, USART_INT_FLAG_IDLE) == SET) {
        if (s->port.idleCallback) {
            s->port.idleCallback();
        }
        /* Clear IDLE by reading STAT then DATA. */
        (void)REG32(usart + 0x00U);
        (void)REG32(usart + 0x04U);
    }
}

void uartTryStartTxDMA(uartPort_t *s)
{
    (void)s;
}

void uartDmaIrqHandler(dmaChannelDescriptor_t *descriptor)
{
    (void)descriptor;
}

#endif /* USE_UART */
