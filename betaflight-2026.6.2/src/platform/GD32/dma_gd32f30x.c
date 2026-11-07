/*
 * This file is part of Betaflight.
 *
 * GD32F30x DMA controller.  Only the descriptor table and the small platform
 * hooks are provided here; the per-peripheral DMA configuration is done by
 * whichever driver requests a channel.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_DMA

#include "drivers/nvic.h"
#include "drivers/dma_impl.h"
#include "platform/dma.h"
#include "platform/rcc.h"
#include "drivers/resource.h"

dmaChannelDescriptor_t dmaDescriptors[DMA_LAST_HANDLER] = {
    DEFINE_DMA_CHANNEL(0, 0, 0),
    DEFINE_DMA_CHANNEL(0, 1, 0),
    DEFINE_DMA_CHANNEL(0, 2, 0),
    DEFINE_DMA_CHANNEL(0, 3, 0),
    DEFINE_DMA_CHANNEL(0, 4, 0),
    DEFINE_DMA_CHANNEL(0, 5, 0),
    DEFINE_DMA_CHANNEL(0, 6, 0),

    DEFINE_DMA_CHANNEL(1, 0, 0),
    DEFINE_DMA_CHANNEL(1, 1, 0),
    DEFINE_DMA_CHANNEL(1, 2, 0),
    DEFINE_DMA_CHANNEL(1, 3, 0),
    DEFINE_DMA_CHANNEL(1, 4, 0),
};

static void enableDmaClock(int index)
{
    const dmaIdentifier_e id = index + DMA_FIRST_HANDLER;
    RCC_ClockCmd((id <= DMA1_CH6_HANDLER) ? RCC_AHB1(DMA0) : RCC_AHB1(DMA1), ENABLE);
}

void dmaEnable(dmaIdentifier_e identifier)
{
    if (identifier < DMA_FIRST_HANDLER || identifier > DMA_LAST_HANDLER) return;
    enableDmaClock(DMA_IDENTIFIER_TO_INDEX(identifier));
}

void dmaSetHandler(dmaIdentifier_e identifier, dmaCallbackHandlerFuncPtr callback, uint32_t priority, uint32_t userParam)
{
    const int index = DMA_IDENTIFIER_TO_INDEX(identifier);
    if (index < 0 || index >= DMA_LAST_HANDLER) return;

    enableDmaClock(index);
    dmaDescriptors[index].irqHandlerCallback = callback;
    dmaDescriptors[index].userParam = userParam;
    __DMB();

    nvic_irq_enable(dmaDescriptors[index].irqN,
                    NVIC_PRIORITY_BASE(priority),
                    NVIC_PRIORITY_SUB(priority));
}

int dmaGetHandlerCount(void)
{
    return DMA_LAST_HANDLER;
}

int dmaGetDeviceNumber(dmaIdentifier_e identifier)
{
    return DMA_DEVICE_NO(identifier);
}

int dmaGetDeviceIndex(dmaIdentifier_e identifier)
{
    return DMA_DEVICE_INDEX(identifier);
}

const char *dmaGetDisplayString(void)
{
    return DMA_OUTPUT_STRING;
}

uint32_t dmaGetDataLength(dmaResource_t *ref)
{
    /* channel CNT register is at ref + 0x04 */
    return REG32((uint32_t)ref + 0x04U) & 0xFFFFU;
}

void dmaMuxEnable(dmaIdentifier_e identifier, uint32_t dmaMuxId)
{
    (void)identifier;
    (void)dmaMuxId;
}

static void dispatch(unsigned index)
{
    dmaChannelDescriptor_t *descriptor = &dmaDescriptors[index];
    const uint32_t controller = (uint32_t)descriptor->dma;
    const unsigned channel = descriptor->channel;
    const uint32_t pending = (DMA_INTF(controller) >> (4U * channel)) & 0x0eU;
    // The shared DMA1 CH3/4 IRQ must service both channels independently.
    if (!(pending & REG32((uint32_t)descriptor->ref))) return;
    if (descriptor->irqHandlerCallback) {
        descriptor->irqHandlerCallback(descriptor);
    } else {
        dma_channel_disable(controller, channel);
        DMA_INTC(controller) = 0x0fU << (4U * channel);
    }
}

#define DMA_IRQ(controller, channel, index) \
    void DMA##controller##_Channel##channel##_IRQHandler(void) { dispatch(index); }
DMA_IRQ(0, 0, 0)
DMA_IRQ(0, 1, 1)
DMA_IRQ(0, 2, 2)
DMA_IRQ(0, 3, 3)
DMA_IRQ(0, 4, 4)
DMA_IRQ(0, 5, 5)
DMA_IRQ(0, 6, 6)
DMA_IRQ(1, 0, 7)
DMA_IRQ(1, 1, 8)
DMA_IRQ(1, 2, 9)
void DMA1_Channel3_4_IRQHandler(void) { dispatch(10); dispatch(11); }

#endif /* USE_DMA */
