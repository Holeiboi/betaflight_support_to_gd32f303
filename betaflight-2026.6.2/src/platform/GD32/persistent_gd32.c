/*
 * This file is part of Betaflight.
 *
 * Persistent data storage on the GD32F30x backup registers.
 *
 * GD32F30x BKP data registers are 16 bits wide, while the Betaflight
 * persistent API is 32 bits.  Use two consecutive BKP registers per object.
 */

#include <stdint.h>

#include "platform.h"

#include "drivers/persistent.h"

#define BKP_REGS_PER_OBJECT     2

static inline bkp_data_register_enum bkpRegisterFor(unsigned index)
{
    return (bkp_data_register_enum)((unsigned)BKP_DATA_0 + index);
}

uint32_t persistentObjectRead(persistentObjectId_e id)
{
    const unsigned base = (unsigned)id * BKP_REGS_PER_OBJECT;
    const uint32_t lo = bkp_read_data(bkpRegisterFor(base));
    const uint32_t hi = bkp_read_data(bkpRegisterFor(base + 1));

    return (hi << 16) | lo;
}

void persistentObjectWrite(persistentObjectId_e id, uint32_t value)
{
    const unsigned base = (unsigned)id * BKP_REGS_PER_OBJECT;

    bkp_write_data(bkpRegisterFor(base), (uint16_t)(value & 0xFFFFU));
    bkp_write_data(bkpRegisterFor(base + 1), (uint16_t)(value >> 16));
}

void persistentObjectInit(void)
{
    rcu_periph_clock_enable(RCU_PMU);
    rcu_periph_clock_enable(RCU_BKPI);

    pmu_backup_write_enable();

    uint32_t wasSoftReset = RCU_RSTSCK & RCU_RSTSCK_SWRSTF;

    if (!wasSoftReset || (persistentObjectRead(PERSISTENT_OBJECT_MAGIC) != 0x42656631U)) {
        for (int i = PERSISTENT_OBJECT_MAGIC + 1; i < PERSISTENT_OBJECT_COUNT; i++) {
            persistentObjectWrite((persistentObjectId_e)i, 0);
        }
        persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, 0x42656631U);
    }
}
