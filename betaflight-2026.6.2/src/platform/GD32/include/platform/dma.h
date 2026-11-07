/*
 * This file is part of Betaflight.
 *
 * DMA definitions for GD32F30x.
 *
 * GD32F303 has two DMA controllers with channels (not streams):
 *   DMA0: 7 channels, DMA1: 5 channels.
 */

#pragma once

#include "platform.h"
#include "drivers/dma.h"

#define DMA_TRAIT_CHANNEL 1

/* GD32 DMA channel register block starts at 0x08, stride 0x14. */
#define GD32_DMA_CHANNEL_BASE(controller, ch) \
    ((controller) + 0x08U + 0x14U * (ch))

#define DMA1_CH0_HANDLER    (DMA_FIRST_HANDLER + 0)
#define DMA1_CH1_HANDLER    (DMA_FIRST_HANDLER + 1)
#define DMA1_CH2_HANDLER    (DMA_FIRST_HANDLER + 2)
#define DMA1_CH3_HANDLER    (DMA_FIRST_HANDLER + 3)
#define DMA1_CH4_HANDLER    (DMA_FIRST_HANDLER + 4)
#define DMA1_CH5_HANDLER    (DMA_FIRST_HANDLER + 5)
#define DMA1_CH6_HANDLER    (DMA_FIRST_HANDLER + 6)
#define DMA2_CH0_HANDLER    (DMA_FIRST_HANDLER + 7)
#define DMA2_CH1_HANDLER    (DMA_FIRST_HANDLER + 8)
#define DMA2_CH2_HANDLER    (DMA_FIRST_HANDLER + 9)
#define DMA2_CH3_HANDLER    (DMA_FIRST_HANDLER + 10)
#define DMA2_CH4_HANDLER    (DMA_FIRST_HANDLER + 11)
#define DMA_LAST_HANDLER    DMA2_CH4_HANDLER

/* GD32 shares one IRQ between DMA1 channel 3 and channel 4. */
#define DMA1_Channel3_IRQn  DMA1_Channel3_Channel4_IRQn
#define DMA1_Channel4_IRQn  DMA1_Channel3_Channel4_IRQn

#define DMA_DEVICE_NO(x)    (((x) <= 7) ? 1 : 2)
#define DMA_DEVICE_INDEX(x) ((((x) - 1) % 7))
#define DMA_OUTPUT_INDEX    0
#define DMA_OUTPUT_STRING   "DMA%d Channel %d:"

#define DEFINE_DMA_CHANNEL(d, ch, f) { \
    .dma = (void *)DMA ## d, \
    .ref = (dmaResource_t *)GD32_DMA_CHANNEL_BASE(DMA ## d, ch), \
    .stream = (ch), \
    .channel = (ch), \
    .irqHandlerCallback = NULL, \
    .flagsShift = 4 * (ch), \
    .irqN = DMA ## d ## _Channel ## ch ## _IRQn, \
    .userParam = 0, \
    .resourceOwner = { .owner = 0, .index = 0 }, \
    .completeFlag = DMA_INT_FLAG_FTF, \
    .dmamux = NULL, \
    }
