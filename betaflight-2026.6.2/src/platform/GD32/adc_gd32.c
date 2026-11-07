/* GPL-3.0-or-later. PC0 / ADC0 channel 10 battery divider: 100k + 10k. */
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "drivers/adc.h"
#include "drivers/io.h"
#include "drivers/time.h"
#include "pg/adc.h"

static bool enabled;

static bool calibrate(uint32_t bit) {
    const uint32_t start = DWT->CYCCNT;
    ADC_CTL1(ADC0) |= bit;
    while (ADC_CTL1(ADC0) & bit) {
        if ((uint32_t)(DWT->CYCCNT - start) > SystemCoreClock / 1000U) return false;
    }
    return true;
}

void adcInit(const adcConfig_t *config) {
    enabled = false;
    // Other ADC inputs are not implemented by this board port.
    if (!config->vbat.enabled || config->vbat.ioTag != IO_TAG(PC0)) return;
    IO_t pin = IOGetByTag(config->vbat.ioTag);
    if (!pin || IOGetOwner(pin) != OWNER_FREE) return;
    IOInit(pin, OWNER_ADC_BATT, 0);
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_AIN, GPIO_OSPEED_2MHZ, GPIO_PIN_0);
    rcu_periph_clock_enable(RCU_ADC0);
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV12); // 10 MHz with 120 MHz APB2
    adc_deinit(ADC0);
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, ENABLE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    adc_channel_length_config(ADC0, ADC_ROUTINE_CHANNEL, 1);
    adc_routine_channel_config(ADC0, 0, ADC_CHANNEL_10, ADC_SAMPLETIME_239POINT5);
    adc_external_trigger_source_config(ADC0, ADC_ROUTINE_CHANNEL, ADC0_1_2_EXTTRIG_ROUTINE_NONE);
    adc_external_trigger_config(ADC0, ADC_ROUTINE_CHANNEL, ENABLE);
    adc_enable(ADC0);
    delay(1);
    if (!calibrate(ADC_CTL1_RSTCLB) || !calibrate(ADC_CTL1_CLB)) {
        adc_disable(ADC0);
        return;
    }
    adc_software_trigger_enable(ADC0, ADC_ROUTINE_CHANNEL);
    enabled = true;
}

uint16_t adcGetValue(adcSource_e source) {
    // Continuous hardware conversions need neither DMA nor a CPU wait loop.
    return enabled && source == ADC_BATTERY ? adc_routine_data_read(ADC0) : 0;
}
