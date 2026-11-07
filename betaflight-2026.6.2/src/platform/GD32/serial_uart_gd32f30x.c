/*
 * This file is part of Betaflight.
 *
 * UART hardware table for GD32F303.
 *
 * The port names deliberately match the board silkscreen:
 *   UART1 -> USART0 (PA9/PA10)
 *   UART2 -> USART2 (PB10/PB11)
 *   UART3 -> UART3  (PC10/PC11)
 *   UART4 -> UART4  (PC12/PD2)
 * Betaflight has no UART0, so the first port (UART1) carries USART0.
 * Pin selection uses AFIO remap bits; 0 means the reset/default mapping.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_UART

#include "build/debug.h"

#include "drivers/system.h"
#include "drivers/io.h"
#include "drivers/dma.h"
#include "platform/dma.h"
#include "drivers/nvic.h"
#include "platform/rcc.h"

#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"

const uartHardware_t uartHardware[UARTDEV_COUNT] = {
#ifdef USE_UART1
    {
        .identifier = SERIAL_PORT_USART1,
        .reg = (usartResource_t *)USART0,
        .rxPins = { { DEFIO_TAG_E(PA10) } },
        .txPins = { { DEFIO_TAG_E(PA9) } },
        .af = GPIO_AF_NONE,
        .rcc = RCC_APB2(USART0),
        .irqn = USART0_IRQn,
        .txPriority = NVIC_PRIO_SERIALUART1_TXDMA,
        .rxPriority = NVIC_PRIO_SERIALUART1,
        .txBuffer = uart1TxBuffer,
        .rxBuffer = uart1RxBuffer,
        .txBufferSize = sizeof(uart1TxBuffer),
        .rxBufferSize = sizeof(uart1RxBuffer),
    },
#endif
#ifdef USE_UART2
    {
        .identifier = SERIAL_PORT_USART2,
        .reg = (usartResource_t *)USART2,
        .rxPins = { { DEFIO_TAG_E(PB11) } },
        .txPins = { { DEFIO_TAG_E(PB10) } },
        .af = GPIO_AF_NONE,
        .rcc = RCC_APB1(USART2),
        .irqn = USART2_IRQn,
        .txPriority = NVIC_PRIO_SERIALUART2_TXDMA,
        .rxPriority = NVIC_PRIO_SERIALUART2,
        .txBuffer = uart2TxBuffer,
        .rxBuffer = uart2RxBuffer,
        .txBufferSize = sizeof(uart2TxBuffer),
        .rxBufferSize = sizeof(uart2RxBuffer),
    },
#endif
#ifdef USE_UART3
    {
        .identifier = SERIAL_PORT_USART3,
        .reg = (usartResource_t *)UART3,
        .rxPins = { { DEFIO_TAG_E(PC11) } },
        .txPins = { { DEFIO_TAG_E(PC10) } },
        .af = GPIO_AF_NONE,
        .rcc = RCC_APB1(UART3),
        .irqn = UART3_IRQn,
        .txPriority = NVIC_PRIO_SERIALUART3_TXDMA,
        .rxPriority = NVIC_PRIO_SERIALUART3,
        .txBuffer = uart3TxBuffer,
        .rxBuffer = uart3RxBuffer,
        .txBufferSize = sizeof(uart3TxBuffer),
        .rxBufferSize = sizeof(uart3RxBuffer),
    },
#endif
#ifdef USE_UART4
    {
        .identifier = SERIAL_PORT_UART4,
        .reg = (usartResource_t *)UART4,
        .rxPins = { { DEFIO_TAG_E(PD2) } },
        .txPins = { { DEFIO_TAG_E(PC12) } },
        .af = GPIO_AF_NONE,
        .rcc = RCC_APB1(UART4),
        .irqn = UART4_IRQn,
        .txPriority = NVIC_PRIO_SERIALUART4_TXDMA,
        .rxPriority = NVIC_PRIO_SERIALUART4,
        .txBuffer = uart4TxBuffer,
        .rxBuffer = uart4RxBuffer,
        .txBufferSize = sizeof(uart4TxBuffer),
        .rxBufferSize = sizeof(uart4RxBuffer),
    },
#endif
};

#endif /* USE_UART */
