/*
 * This file is part of Betaflight.
 *
 * GPIO access for GD32F30x (classic F1-style GPIO + AFIO remap).
 */

#include "platform.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "platform/io_impl.h"
#include "platform/rcc.h"

#include "common/utils.h"

struct ioPortDef_s {
    rccPeriphTag_t rcc;
};

const struct ioPortDef_s ioPortDefs[] = {
    { RCC_APB2(GPIOA) },
    { RCC_APB2(GPIOB) },
    { RCC_APB2(GPIOC) },
    { RCC_APB2(GPIOD) },
};

uint32_t IO_EXTI_Line(IO_t io)
{
    if (!io) {
        return 0;
    }
    return IO_Pin(io);
}

bool IORead(IO_t io)
{
    if (!io) {
        return false;
    }
    return gpio_input_bit_get((uint32_t)IO_GPIO(io), IO_Pin(io)) != RESET;
}

void IOWrite(IO_t io, bool hi)
{
    if (!io) {
        return;
    }
    gpio_bit_write((uint32_t)IO_GPIO(io), IO_Pin(io), hi ? SET : RESET);
}

void IOHi(IO_t io)
{
    if (!io) {
        return;
    }
    gpio_bit_set((uint32_t)IO_GPIO(io), IO_Pin(io));
}

void IOLo(IO_t io)
{
    if (!io) {
        return;
    }
    gpio_bit_reset((uint32_t)IO_GPIO(io), IO_Pin(io));
}

void IOToggle(IO_t io)
{
    if (!io) {
        return;
    }
    uint32_t pin = IO_Pin(io);
    if (gpio_output_bit_get((uint32_t)IO_GPIO(io), pin)) {
        gpio_bit_reset((uint32_t)IO_GPIO(io), pin);
    } else {
        gpio_bit_set((uint32_t)IO_GPIO(io), pin);
    }
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg)
{
    IOConfigGPIOAF(io, cfg, 0);
}

void IOConfigGPIOAF(IO_t io, ioConfig_t cfg, uint8_t af)
{
    if (!io) {
        return;
    }

    const uint32_t mode = cfg & 0x1FU;
    uint32_t speed = (cfg >> 5) & 0x03U;
    if (speed == 0) {
        speed = GPIO_OSPEED_50MHZ;
    }

    RCC_ClockCmd(ioPortDefs[IO_GPIOPortIdx(io)].rcc, ENABLE);

    gpio_init((uint32_t)IO_GPIO(io), mode, speed, IO_Pin(io));

    /*
     * GD32F30x has no per-pin AF number: the alternate function is selected
     * by AFIO remap bits.  The caller passes an AFIO_PCF0 remap mask here
     * (0 = reset/default pin mapping).
     */
    if (af) {
        gpio_pin_remap_config(af, ENABLE);
    }
}
