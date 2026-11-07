/*
 * This file is part of Betaflight.
 *
 * Betaflight MCU system hooks for GD32F303.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#include "drivers/nvic.h"
#include "drivers/persistent.h"
#include "drivers/system.h"
#include "drivers/io.h"

extern uint32_t cachedResetFlags;

/* The minimal port has no C++ constructors or runtime init hooks. */
void _init(void) {}

void debugInit(void)
{
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    IOInit(IOGetByTag(IO_TAG(PA13)), OWNER_SWD, 0);
    IOInit(IOGetByTag(IO_TAG(PA14)), OWNER_SWD, 0);
}

void SetSysClock(void);
void SystemCoreClockUpdate(void);

void systemReset(void)
{
    __disable_irq();
    NVIC_SystemReset();
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    (void)requestType;

    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_BOOTLOADER_REQUEST_ROM);

    __disable_irq();
    NVIC_SystemReset();
}

void checkForBootLoaderRequest(void)
{
    /* ROM bootloader entry is not wired up yet.  Keep the hook so the rest
     * of the system can call it unconditionally. */
}

bool isMPUSoftReset(void)
{
    return (cachedResetFlags & RCU_RSTSCK_SWRSTF) != 0;
}

void systemInit(void)
{
    persistentObjectInit();

    checkForBootLoaderRequest();

    SetSysClock();

    nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);

    cachedResetFlags = RCU_RSTSCK;

    extern uint8_t isr_vector_table_base;
    SCB->VTOR = (uint32_t)&isr_vector_table_base;

    rcu_all_reset_flag_clear();

    cycleCounterInit();

    SysTick_Config(SystemCoreClock / 1000);
}
