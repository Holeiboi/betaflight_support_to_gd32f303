/*
 * This file is part of Betaflight.
 *
 * GD32F303 clock / FPU bring-up.  Called from the startup file as SystemInit().
 *
 * Target: HXTAL 8MHz /2 -> PLL x30 -> 120MHz SYSCLK
 *         AHB /1 (120MHz), APB2 /1 (120MHz), APB1 /2 (60MHz)
 *         Flash 2 wait states, USB clock = PLL / 2.5 = 48MHz
 */

#include <stdint.h>

#include "platform.h"

uint32_t SystemCoreClock = 120000000U;

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = rcu_clock_freq_get(CK_SYS);
}

void SetSysClock(void)
{
    /* Already on PLL?  systemInit() calls us a second time. */
    if ((RCU_CTL & RCU_CTL_PLLSTB) && ((RCU_CFG0 & RCU_CFG0_SCSS) == RCU_SCSS_PLL)) {
        SystemCoreClockUpdate();
        return;
    }

    /* Configure Flash before raising the core clock. */
    fmc_wscnt_set(WS_WSCNT_2);

    /* HXTAL on, wait for stable */
    rcu_osci_on(RCU_HXTAL);
    if (rcu_osci_stab_wait(RCU_HXTAL) == ERROR) {
        /* No external clock: stay on IRC8M.  The board is not usable like
         * this, but at least it does not hang. */
        SystemCoreClockUpdate();
        return;
    }

    /* Match the vendor SDK's 120 MHz voltage and high-drive sequence. */
    rcu_periph_clock_enable(RCU_PMU);
    PMU_CTL |= PMU_CTL_LDOVS;

    rcu_ahb_clock_config(RCU_AHB_CKSYS_DIV1);
    rcu_apb2_clock_config(RCU_APB2_CKAHB_DIV1);
    rcu_apb1_clock_config(RCU_APB1_CKAHB_DIV2);

    /* PLL source = HXTAL/2, multiplier = 30 -> 120MHz. */
    RCU_CFG0 |= RCU_CFG0_PREDV0;
    rcu_pll_config(RCU_PLLSRC_HXTAL_IRC48M, RCU_PLL_MUL30);

    /* USB clock must be 48MHz: 120 / 2.5. */
    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);

    rcu_osci_on(RCU_PLL_CK);
    while (rcu_osci_stab_wait(RCU_PLL_CK) == ERROR) {
    }

    PMU_CTL |= PMU_CTL_HDEN;
    while (!(PMU_CS & PMU_CS_HDRF)) {}
    PMU_CTL |= PMU_CTL_HDS;
    while (!(PMU_CS & PMU_CS_HDSRF)) {}

    rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
    while (rcu_system_clock_source_get() != RCU_SCSS_PLL) {
    }

    SystemCoreClockUpdate();
}

void SystemInit(void)
{
    /* Enable the FPU (CP10/CP11 full access) before any float code runs. */
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));

    SetSysClock();
}
