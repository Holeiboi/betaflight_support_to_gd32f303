/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#ifdef GD32F3

#include <stdint.h>

#include "gd32f30x.h"

#include "gd32f30x_adc.h"
#include "gd32f30x_bkp.h"
#include "gd32f30x_dma.h"
// Storage type used by shared bus structures. GD32 peripheral configuration
// remains native; this alias does not enable SPI/UART DMA automatically.
typedef dma_parameter_struct DMA_InitTypeDef;
#include "gd32f30x_exti.h"
#include "gd32f30x_fmc.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_i2c.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_pmu.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_rtc.h"
#include "gd32f30x_spi.h"
#include "gd32f30x_timer.h"
#include "gd32f30x_usart.h"

/* BF common/utils.h defines its own BIT(); the GD32 headers have one too. */
#undef BIT

/*
 * Betaflight talks to GPIO ports through ioRec_t.gpio, which is filled from
 * GPIOA_BASE + (port << 10) by common/stm32/io_impl.c.  GD32 peripherals are
 * plain addresses (REG32-style), so provide a struct with the same layout and
 * the STM32 field names used by the few places that poke registers directly.
 */
typedef struct {
    volatile uint32_t CTL0;
    volatile uint32_t CTL1;
    union {
        volatile uint32_t ISTAT;
        volatile uint32_t IDR;
    };
    union {
        volatile uint32_t OCTL;
        volatile uint32_t ODR;
    };
    union {
        volatile uint32_t BOP;
        volatile uint32_t BSRR;
    };
    union {
        volatile uint32_t BC;
        volatile uint32_t BRR;
    };
    volatile uint32_t LOCK;
} GPIO_TypeDef;

#define GPIOA_BASE  (GPIO_BASE + 0x00000000U)
#define GPIOB_BASE  (GPIO_BASE + 0x00000400U)
#define GPIOC_BASE  (GPIO_BASE + 0x00000800U)
#define GPIOD_BASE  (GPIO_BASE + 0x00000C00U)
#define GPIOE_BASE  (GPIO_BASE + 0x00001000U)
#define GPIOF_BASE  (GPIO_BASE + 0x00001400U)
#define GPIOG_BASE  (GPIO_BASE + 0x00001800U)

typedef ControlStatus FunctionalState;

/* Unique device ID, same layout as STM32F1/F3 */
#define U_ID_0  (*(uint32_t *)0x1FFFF7E8U)
#define U_ID_1  (*(uint32_t *)0x1FFFF7ECU)
#define U_ID_2  (*(uint32_t *)0x1FFFF7F0U)

/*
 * IO configuration packing.
 *
 * GD32 GPIO modes are the classic 4-bit values (0x04 floating input,
 * 0x10 output PP, 0x18 AF PP, ...).  Put the mode in bits 0..4 and the
 * output speed in bits 5..6.  Pull-up/down is part of the mode itself.
 */
#define IO_CONFIG(mode, speed, pupd) \
    (((uint8_t)(mode)) | (((uint8_t)(speed)) << 5))
#define IO_CONFIG_PULL(mode, speed, pupd) IO_CONFIG(mode, speed, pupd)

#define IOCFG_OUT_PP          IO_CONFIG(GPIO_MODE_OUT_PP,      GPIO_OSPEED_50MHZ, 0)
#define IOCFG_OUT_PP_UP       IO_CONFIG(GPIO_MODE_OUT_PP,      GPIO_OSPEED_50MHZ, 0)
#define IOCFG_OUT_PP_25       IO_CONFIG(GPIO_MODE_OUT_PP,      GPIO_OSPEED_50MHZ, 0)
#define IOCFG_OUT_OD          IO_CONFIG(GPIO_MODE_OUT_OD,      GPIO_OSPEED_50MHZ, 0)
#define IOCFG_AF_PP           IO_CONFIG(GPIO_MODE_AF_PP,       GPIO_OSPEED_50MHZ, 0)
#define IOCFG_AF_PP_PD        IO_CONFIG(GPIO_MODE_AF_PP,       GPIO_OSPEED_50MHZ, 0)
#define IOCFG_AF_PP_UP        IO_CONFIG(GPIO_MODE_AF_PP,       GPIO_OSPEED_50MHZ, 0)
#define IOCFG_AF_OD           IO_CONFIG(GPIO_MODE_AF_OD,       GPIO_OSPEED_50MHZ, 0)
#define IOCFG_IPD             IO_CONFIG(GPIO_MODE_IPD,         GPIO_OSPEED_2MHZ,  0)
#define IOCFG_IPU             IO_CONFIG(GPIO_MODE_IPU,         GPIO_OSPEED_2MHZ,  0)
#define IOCFG_IN_FLOATING     IO_CONFIG(GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,  0)
#define IOCFG_IPU_25          IO_CONFIG(GPIO_MODE_IPU,         GPIO_OSPEED_50MHZ, 0)
#define IOCFG_AIN             IO_CONFIG(GPIO_MODE_AIN,         GPIO_OSPEED_2MHZ,  0)

#define UARTHARDWARE_MAX_PINS       4
#define SPI_IO_CS_CFG              IOCFG_OUT_PP
#define MAX_SPI_PIN_SEL            4
#define UART_TX_BUFFER_ATTRIBUTE
#define UART_RX_BUFFER_ATTRIBUTE
#define FAST_IRQ_HANDLER
#define FAST_CODE
#define FAST_CODE_NOINLINE          NOINLINE
#define FAST_DATA_ZERO_INIT         __attribute__ ((section(".fastram_bss"), aligned(4)))
#define FAST_DATA                   __attribute__ ((section(".fastram_data"), aligned(4)))
#define DMA_DATA_ZERO_INIT
#define DMA_DATA
#define STATIC_DMA_DATA_AUTO        static
#define SPIDEV_COUNT               3
#define I2CDEV_COUNT               1
#define DEFAULT_CPU_OVERCLOCK      0
#define SCHEDULER_DELAY_LIMIT      10
#define DMA_TRAIT_CHANNEL          1
#define USE_TIMER_AF               1
#define FLASH_CONFIG_BUFFER_TYPE    uint32_t

/* Platform traits used by the shared drivers */
#define PLATFORM_TRAIT_RCC        1
#define SERIAL_TRAIT_PIN_CONFIG   1
#define UART_TRAIT_AF_PORT        1

/* NVIC priority packing (2 bits pre-emption, 2 bits sub-priority). */
#define NVIC_PRIORITY_GROUPING      5   /* CMSIS grouping: 2 pre / 2 sub */
#define NVIC_BUILD_PRIORITY(base, sub) \
    (((((base) << (4 - (7 - (NVIC_PRIORITY_GROUPING)))) | \
       ((sub) & (0x0f >> (7 - (NVIC_PRIORITY_GROUPING))))) << 4) & 0xf0)
#define NVIC_PRIORITY_BASE(prio)    (((prio) >> (4 - (7 - (NVIC_PRIORITY_GROUPING)))) >> 4)
#define NVIC_PRIORITY_SUB(prio)     (((prio) >> 4) & (0x0f >> (7 - (NVIC_PRIORITY_GROUPING))))

/*
 * GD32F30x selects alternate functions through AFIO remap bits, not AF
 * numbers.  The uartHardware_t.af field is reused as an AFIO_PCF0 remap
 * mask: 0 means the reset (default) pin mapping.
 */
#define GPIO_AF_NONE              0
#define GPIO_AF_USART0_REMAP      AFIO_PCF0_USART0_REMAP
#define GPIO_AF_USART1_REMAP      AFIO_PCF0_USART1_REMAP
#define GPIO_AF_USART2_REMAP      AFIO_PCF0_USART2_REMAP
#define GPIO_AF_SPI0_REMAP        AFIO_PCF0_SPI0_REMAP
#define GPIO_AF_I2C0_REMAP        AFIO_PCF0_I2C0_REMAP

/* USART data register (GD32 USART_DATA is at offset 0x04) */
#define UART_REG_RXD(base)  REG32((uint32_t)(base) + 0x04U)
#define UART_REG_TXD(base)  REG32((uint32_t)(base) + 0x04U)

extern uint32_t SystemCoreClock;

/* Compatibility names used by the shared UART table code */
#define GPIO_AF7_USART1           GPIO_AF_NONE
#define GPIO_AF7_USART2           GPIO_AF_NONE
#define GPIO_AF7_USART3           GPIO_AF_NONE

#endif /* GD32F3 */
