/*
 * GPL-3.0-or-later. Polled SPI0/SPI1/SPI2 for the GD32F303 port.
 *
 * GD32F30x is F1-style: SPI pins are selected through AFIO remap bits, the
 * peripheral is addressed by its base address, and the data register is
 * accessed with the standard peripheral library.  DMA is not wired up yet, so
 * every transfer runs through spiProcessSegmentsPolled() and completes before
 * spiSequence() returns.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_SPI

#include "common/maths.h"
#include "common/utils.h"

#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/io.h"
#include "drivers/nvic.h"

#include "pg/bus_spi.h"

extern busDevice_t spiBusDevice[SPIDEV_COUNT];
extern spiDevice_t spiDevice[SPIDEV_COUNT];

void spiInitBusDMA(void)
{
    // DMA0 CH1 is reserved for TIMER1_UP. SPI DMA requires a separate backend
    // and conflict-aware allocation; leave all SPI buses explicitly polled.
    for (unsigned i = 0; i < SPIDEV_COUNT; i++) spiBusDevice[i].useDMA = false;
}

const spiHardware_t spiHardware[SPIDEV_COUNT] = {
    {
        .device = SPIDEV_0,
        .reg = (spiResource_t *)(uintptr_t)SPI0,
        .rcc = RCU_SPI0,
        .sckPins = { { IO_TAG(PA5) } },
        .misoPins = { { IO_TAG(PA6) } },
        .mosiPins = { { IO_TAG(PA7) } },
    },
    {
        .device = SPIDEV_1,
        .reg = (spiResource_t *)(uintptr_t)SPI1,
        .rcc = RCU_SPI1,
        .sckPins = { { IO_TAG(PB13) } },
        .misoPins = { { IO_TAG(PB14) } },
        .mosiPins = { { IO_TAG(PB15) } },
    },
#ifdef USE_SPI_DEVICE_2
    {
        .device = SPIDEV_2,
        .reg = (spiResource_t *)(uintptr_t)SPI2,
        .rcc = RCU_SPI2,
        .sckPins = { { IO_TAG(PB3) } },
        .misoPins = { { IO_TAG(PB4) } },
        .mosiPins = { { IO_TAG(PB5) } },
    },
#else
    { .device = SPIDEV_2, .reg = NULL },
#endif
};

static rcu_periph_enum spiRccFor(spiDevice_e device)
{
    switch (device) {
    case SPIDEV_0:
        return RCU_SPI0;
    case SPIDEV_1:
        return RCU_SPI1;
    case SPIDEV_2:
        return RCU_SPI2;
    default:
        return RCU_SPI0;
    }
}

static uint32_t pscForDivisor(uint16_t divisor)
{
    switch (divisor) {
    case 2:
        return SPI_PSC_2;
    case 4:
        return SPI_PSC_4;
    case 8:
        return SPI_PSC_8;
    case 16:
        return SPI_PSC_16;
    case 32:
        return SPI_PSC_32;
    case 64:
        return SPI_PSC_64;
    case 128:
        return SPI_PSC_128;
    case 256:
    default:
        return SPI_PSC_256;
    }
}

void spiPinConfigure(const struct spiPinConfig_s *pConfig)
{
    for (size_t hwindex = 0; hwindex < ARRAYLEN(spiHardware); hwindex++) {
        const spiHardware_t *hw = &spiHardware[hwindex];

        if (!hw->reg) {
            continue;
        }

        const spiDevice_e device = hw->device;
        spiDevice_t *pDev = &spiDevice[device];
        pDev->dev = NULL;
        pDev->sck = pDev->miso = pDev->mosi = IO_TAG_NONE;

        for (int pindex = 0; pindex < MAX_SPI_PIN_SEL; pindex++) {
            if (pConfig[device].ioTagSck == hw->sckPins[pindex].pin) {
                pDev->sck = hw->sckPins[pindex].pin;
            }
            if (pConfig[device].ioTagMiso == hw->misoPins[pindex].pin) {
                pDev->miso = hw->misoPins[pindex].pin;
            }
            if (pConfig[device].ioTagMosi == hw->mosiPins[pindex].pin) {
                pDev->mosi = hw->mosiPins[pindex].pin;
            }
        }

        if (pDev->sck && pDev->miso && pDev->mosi) {
            pDev->dev = hw->reg;
            pDev->leadingEdge = false;
        }
    }
}

static uint32_t spiReferenceClock(void)
{
    // The public divider API has no bus argument. Use the faster APB clock
    // as its reference, then translate to the selected peripheral below.
    return MAX(rcu_clock_freq_get(CK_APB1), rcu_clock_freq_get(CK_APB2));
}

static void spiConfigureInstance(uint32_t instance, uint16_t divisor, bool leadingEdge)
{
    spi_parameter_struct params;
    const uint32_t inputClock = rcu_clock_freq_get(instance == SPI0 ? CK_APB2 : CK_APB1);
    const uint32_t requestedClock = spiReferenceClock() / MAX(divisor, 2U);
    uint16_t physicalDivisor = 2;
    while (physicalDivisor < 256 && inputClock / physicalDivisor > requestedClock) {
        physicalDivisor <<= 1;
    }

    spi_disable(instance);
    spi_struct_para_init(&params);
    params.device_mode = SPI_MASTER;
    params.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    params.frame_size = SPI_FRAMESIZE_8BIT;
    params.nss = SPI_NSS_SOFT;
    params.endian = SPI_ENDIAN_MSB;
    // Match the shared Betaflight API: true = mode 0, false = mode 3.
    params.clock_polarity_phase = leadingEdge ? SPI_CK_PL_LOW_PH_1EDGE : SPI_CK_PL_HIGH_PH_2EDGE;
    params.prescale = pscForDivisor(physicalDivisor);
    spi_init(instance, &params);
    spi_nss_internal_high(instance);
    spi_enable(instance);
}

void spiInitDevice(spiDevice_e device)
{
    if (device < 0 || device >= SPIDEV_COUNT) return;
    spiDevice_t *spi = &spiDevice[device];

    if (!spi->dev) {
        return;
    }

    const uint32_t instance = (uint32_t)spi->dev;

    rcu_periph_clock_enable(spiRccFor(device));
    rcu_periph_clock_enable(RCU_AF);
    if (device == SPIDEV_0) gpio_pin_remap_config(GPIO_SPI0_REMAP, DISABLE);
    if (device == SPIDEV_2) gpio_pin_remap_config(GPIO_SPI2_REMAP, DISABLE);

    IOInit(IOGetByTag(spi->sck), OWNER_SPI_SCK, RESOURCE_INDEX(device));
    IOInit(IOGetByTag(spi->miso), OWNER_SPI_SDI, RESOURCE_INDEX(device));
    IOInit(IOGetByTag(spi->mosi), OWNER_SPI_SDO, RESOURCE_INDEX(device));

    IOConfigGPIOAF(IOGetByTag(spi->sck), IOCFG_AF_PP, 0);
    IOConfigGPIOAF(IOGetByTag(spi->miso), IOCFG_IN_FLOATING, 0);
    IOConfigGPIOAF(IOGetByTag(spi->mosi), IOCFG_AF_PP, 0);

    spiConfigureInstance(instance, 8, spi->leadingEdge);
}

static bool spiWaitFlag(uint32_t instance, uint32_t mask, bool set)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t timeout = SystemCoreClock / 10000U; // 100 us, IRQs stay enabled
    while (((SPI_STAT(instance) & mask) != 0) != set) {
        if ((uint32_t)(DWT->CYCCNT - start) >= timeout) return false;
    }
    return true;
}

bool spiInternalReadWriteBufPolled(spiResource_t *spiInstance, const uint8_t *txData, uint8_t *rxData, int len)
{
    const uint32_t instance = (uint32_t)spiInstance;
    if ((instance != SPI0 && instance != SPI1 && instance != SPI2) || len < 0) return false;

    while (len-- > 0) {
        const uint8_t out = txData ? *(txData++) : 0xFF;

        if (!spiWaitFlag(instance, SPI_STAT_TBE, true)) goto timeout;
        spi_i2s_data_transmit(instance, out);

        if (!spiWaitFlag(instance, SPI_STAT_RBNE, true)) goto timeout;
        const uint8_t in = (uint8_t)spi_i2s_data_receive(instance);
        if (rxData) {
            *(rxData++) = in;
        }
    }

    // Do not release CS or reconfigure SPI before the final wire clock ends.
    if (!spiWaitFlag(instance, SPI_STAT_TRANS, false)) goto timeout;
    return true;
timeout:
    for (unsigned i = 0; i < SPIDEV_COUNT; i++) {
        if (spiDevice[i].dev == spiInstance) spiDevice[i].errorCount++;
    }
    if (rxData && len >= 0) memset(rxData, 0xff, (size_t)len + 1U);
    return false;
}

void spiSequenceStart(const extDevice_t *dev)
{
    busDevice_t *bus = dev->bus;
    const uint32_t instance = (uint32_t)bus->busType_u.spi.instance;

    // This port has no SPI DMA yet; keep the generic layer on the polled path.
    ((extDevice_t *)dev)->useDMA = false;
    bus->useDMA = false;

    if ((dev->busType_u.spi.speed != bus->busType_u.spi.speed) ||
        (dev->busType_u.spi.leadingEdge != bus->busType_u.spi.leadingEdge)) {
        spiConfigureInstance(instance, dev->busType_u.spi.speed, dev->busType_u.spi.leadingEdge);
        bus->busType_u.spi.speed = dev->busType_u.spi.speed;
        bus->busType_u.spi.leadingEdge = dev->busType_u.spi.leadingEdge;
    }

    spiProcessSegmentsPolled(dev);
}

void spiInternalResetDescriptors(busDevice_t *bus)
{
    UNUSED(bus);
}

void spiInternalResetStream(dmaChannelDescriptor_t *descriptor)
{
    UNUSED(descriptor);
}

/*
 * SPI0 is on APB2 and SPI1/SPI2 on APB1; at the supported 120 MHz core clock
 * APB2 is 120 MHz and APB1 is 60 MHz. Dividers use the faster clock as a
 * reference; spiConfigureInstance converts them to each bus's physical PSC.
 * At reference divisor 2 the slower bus is limited to its own input / 2.
 */
uint16_t spiCalculateDivider(uint32_t freq)
{
    uint32_t spiClk = spiReferenceClock();
    uint16_t divisor = 2;

    while ((spiClk / divisor) > freq && divisor < 256) {
        divisor <<= 1;
    }

    return divisor;
}

uint32_t spiCalculateClock(uint16_t spiClkDivisor)
{
    return spiClkDivisor ? spiReferenceClock() / spiClkDivisor : 0;
}

#endif /* USE_SPI */
