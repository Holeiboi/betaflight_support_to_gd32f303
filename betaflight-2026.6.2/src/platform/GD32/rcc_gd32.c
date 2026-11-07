/*
 * This file is part of Betaflight.
 *
 * RCC (RCU) helpers for GD32F30x.
 *
 * The shared platform/rcc.h encodes an rccPeriphTag_t as (bus << 5) | bit for
 * the ST parts.  GD32's rcu_periph_enum already encodes (register offset << 6)
 * | bit, which is richer and maps directly onto rcu_periph_clock_enable(), so
 * for GD32F3 the RCC_APBx() macros simply expand to the GD32 enum value and
 * these helpers pass it straight through.
 */

#include "platform.h"
#include "platform/rcc.h"

void RCC_ClockCmd(rccPeriphTag_t periphTag, FunctionalState NewState)
{
    if (NewState == ENABLE) {
        rcu_periph_clock_enable((rcu_periph_enum)periphTag);
    } else {
        rcu_periph_clock_disable((rcu_periph_enum)periphTag);
    }
}

static uint32_t resetRegisterFor(uint32_t enableRegister)
{
    switch (enableRegister) {
    case AHBEN_REG_OFFSET:
        return AHBRST_REG_OFFSET;
    case APB1EN_REG_OFFSET:
        return APB1RST_REG_OFFSET;
    case APB2EN_REG_OFFSET:
        return APB2RST_REG_OFFSET;
    case ADD_APB1EN_REG_OFFSET:
        return ADD_APB1RST_REG_OFFSET;
    default:
        return enableRegister;
    }
}

void RCC_ResetCmd(rccPeriphTag_t periphTag, FunctionalState NewState)
{
    const uint32_t tag = (uint32_t)periphTag;
    const uint32_t registerOffset = tag >> 6;
    const uint32_t bit = tag & 0x3FU;
    const uint32_t resetTag = RCU_REGIDX_BIT(resetRegisterFor(registerOffset), bit);

    if (NewState == ENABLE) {
        /* Betaflight's semantics: ENABLE releases reset. */
        rcu_periph_reset_disable((rcu_periph_reset_enum)resetTag);
    } else {
        rcu_periph_reset_enable((rcu_periph_reset_enum)resetTag);
    }
}
