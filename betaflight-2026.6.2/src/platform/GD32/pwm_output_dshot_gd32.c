/* GPL-3.0-or-later. Single-direction DShot300/600, TIMER1 CH0..3 DMAR.
 * GD32F30x UM Rev3.4 p219: TIMER1_UP -> DMA0 channel 1.
 * IRQ only stops the completed frame; all bit edges are generated in hardware.
 */
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "platform.h"
#include "drivers/dma.h"
#include "platform/dma.h"
#include "drivers/dshot.h"
#include "drivers/dshot_command.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/time.h"

#define MOTOR_COUNT 4
#define FRAME_ROWS 20 // 16 bits followed by low rows (including preload pipeline)
#define MOTOR_DMA_ID DMA1_CH1_HANDLER // BF identifier for GD32 DMA0 channel 1
static uint16_t waveform[FRAME_ROWS][MOTOR_COUNT];
static dshotProtocolControl_t control[MOTOR_COUNT];
static IO_t pins[MOTOR_COUNT];
static uint8_t channels[MOTOR_COUNT];
static uint16_t period;
static bool initialized, active, blockingRequest;
static volatile bool busy, fault;
static uint32_t frameStarted;
// Keep these visible to SWD diagnostics, including faults without a DMA IRQ.
volatile uint32_t gd32DshotFrames, gd32DshotErrors, gd32DshotOverruns;

static void stopHardware(void)
{
    timer_dma_disable(TIMER1, TIMER_DMA_UPD);
    dma_channel_disable(DMA0, DMA_CH1);
    timer_disable(TIMER1);
    DMA_INTC(DMA0) = 0x0fU << 4;
    for (unsigned ch = 0; ch < MOTOR_COUNT; ch++) {
        timer_channel_output_pulse_value_config(TIMER1, ch, 0);
    }
    timer_event_software_generate(TIMER1, TIMER_EVENT_SRC_UPG);
    busy = false;
}

static void motorDmaIrq(dmaChannelDescriptor_t *descriptor)
{
    (void)descriptor;
    const uint32_t flags = (DMA_INTF(DMA0) >> 4) & 0x0fU;
    if (flags & DMA_INT_FLAG_ERR) {
        fault = true; active = false; gd32DshotErrors++;
    }
    // Completion occurs only once both preloaded compare rows are zero.
    if (flags & (DMA_INT_FLAG_FTF | DMA_INT_FLAG_ERR)) stopHardware();
    else DMA_INTC(DMA0) = flags << 4;
}

static void noOp(void) {}
static bool enable(void) { active = initialized && !fault; return active; }
static void disable(void)
{
    const uint32_t key = __get_PRIMASK(); __disable_irq();
    active = false; blockingRequest = false;
    if (initialized) stopHardware();
    memset(control, 0, sizeof(control));
    __set_PRIMASK(key);
}
static void shutdown(void) { disable(); initialized = false; }
static bool isEnabled(unsigned i) { return active && !fault && i < MOTOR_COUNT; }
static bool isIdle(unsigned i) { return i < MOTOR_COUNT && control[i].value == 0; }
static IO_t getIO(unsigned i) { return i < MOTOR_COUNT ? pins[i] : IO_NONE; }
static void requestTelemetry(unsigned i) { if (i < MOTOR_COUNT) control[i].requestTelemetry = true; }
static void releaseIO(unsigned i)
{
    if (i >= MOTOR_COUNT || active) return;
    timer_channel_output_state_config(TIMER1, channels[i], TIMER_CCX_DISABLE);
    IOConfigGPIO(pins[i], IOCFG_IPU);
}
static bool reinstateIO(unsigned i)
{
    if (i >= MOTOR_COUNT || !initialized || fault || active) return false;
    IOLo(pins[i]);
    IOConfigGPIO(pins[i], IOCFG_AF_PP);
    timer_channel_output_state_config(TIMER1, channels[i], TIMER_CCX_ENABLE);
    return true;
}

static void writeInt(uint8_t i, uint16_t value)
{
    if (!initialized || fault || i >= MOTOR_COUNT) return;
    if (value > DSHOT_MAX_THROTTLE) value = DSHOT_MAX_THROTTLE;
    // Explicit blocking ESC commands are allowed while the motor device is
    // disabled, as required by dshotCommandWrite; throttle is never allowed.
    if (!active) {
        if (value >= DSHOT_MIN_THROTTLE) value = 0;
        else blockingRequest = true;
    }
    control[i].value = value;
}
static void writeMotor(uint8_t i, float value)
{
    if (!active) return;
    writeInt(i, isfinite(value) ? (uint16_t)fminf(DSHOT_MAX_THROTTLE, fmaxf(0, value)) : 0);
}

static void complete(void)
{
    if (!initialized || fault || (!active && !blockingRequest)) return;
    if (busy) {
        gd32DshotOverruns++;
        if ((uint32_t)(DWT->CYCCNT-frameStarted) > SystemCoreClock / 5000U) {
            const uint32_t key = __get_PRIMASK(); __disable_irq();
            stopHardware(); fault = true; active = false; gd32DshotErrors++;
            __set_PRIMASK(key);
        }
        return;
    }
    if (!dshotCommandQueueEmpty() && !dshotCommandOutputIsEnabled(MOTOR_COUNT)) return;
    memset(waveform, 0, sizeof(waveform));
    for (unsigned i = 0; i < MOTOR_COUNT; i++) {
        dshotProtocolControl_t packetControl = control[i];
        control[i].requestTelemetry = false;
        if (dshotCommandIsProcessing()) {
            packetControl.value = dshotCommandGetCurrent(i);
            if (packetControl.value) packetControl.requestTelemetry = true;
        }
        const uint16_t packet = prepareDshotPacket(&packetControl);
        for (unsigned bit = 0; bit < 16; bit++) {
            waveform[bit][channels[i]] = (packet & (0x8000U >> bit)) ? period * 3U / 4U : period * 3U / 8U;
        }
    }

    const uint32_t key = __get_PRIMASK(); __disable_irq();
    stopHardware();
    timer_counter_value_config(TIMER1, 0);
    // Keep active compare zero while stopped; preload bit 0 into shadow.
    // The first hardware overflow starts bit 0 and DMA preloads bit 1.
    // Loading an active high compare with a software UPG while CEN=0 would
    // raise the pin early and stretch the very first DShot pulse.
    for (unsigned ch = 0; ch < MOTOR_COUNT; ch++) timer_channel_output_pulse_value_config(TIMER1, ch, waveform[0][ch]);
    dma_memory_address_config(DMA0, DMA_CH1, (uint32_t)&waveform[1][0]);
    dma_transfer_number_config(DMA0, DMA_CH1, (FRAME_ROWS-1U)*MOTOR_COUNT);
    DMA_INTC(DMA0) = 0x0fU << 4;
    timer_interrupt_flag_clear(TIMER1, TIMER_INT_FLAG_UP);
    __DMB();
    frameStarted = DWT->CYCCNT; busy = true; blockingRequest = false;
    dma_channel_enable(DMA0, DMA_CH1);
    timer_dma_enable(TIMER1, TIMER_DMA_UPD);
    timer_enable(TIMER1);
    gd32DshotFrames++;
    __set_PRIMASK(key);
}

static const motorVTable_t vtable = {
    .postInit=noOp, .convertExternalToMotor=dshotConvertFromExternal,
    .convertMotorToExternal=dshotConvertToExternal, .enable=enable,
    .disable=disable, .isMotorEnabled=isEnabled, .write=writeMotor,
    .writeInt=writeInt, .updateComplete=complete, .shutdown=shutdown,
    .isMotorIdle=isIdle, .getMotorIO=getIO, .requestTelemetry=requestTelemetry,
    .releaseMotorIO=releaseIO, .reinstateMotorIO=reinstateIO,
};

bool dshotPwmDevInit(motorDevice_t *dev, const motorDevConfig_t *cfg)
{
    const ioTag_t tags[MOTOR_COUNT] = {IO_TAG(PA0), IO_TAG(PA1), IO_TAG(PA2), IO_TAG(PA3)};
    const uint32_t rate = cfg->motorProtocol == MOTOR_PROTOCOL_DSHOT300 ? 300000U : 600000U;
    if (dev->count != MOTOR_COUNT ||
        (cfg->motorProtocol != MOTOR_PROTOCOL_DSHOT300 && cfg->motorProtocol != MOTOR_PROTOCOL_DSHOT600) ||
        cfg->useDshotTelemetry || cfg->useDshotEdt || cfg->motorInversion ||
        cfg->useDshotBitbang == DSHOT_BITBANG_ON || cfg->useBurstDshot == DSHOT_DMAR_OFF ||
        SystemCoreClock != 120000000U || rcu_clock_freq_get(CK_APB1) != 60000000U) return false;
    unsigned used = 0;
    for (unsigned i = 0; i < MOTOR_COUNT; i++) {
        if (cfg->ioTags[i] != tags[i] || IOGetOwner(IOGetByTag(tags[i])) != OWNER_FREE) return false;
        const unsigned ch = cfg->motorOutputReordering[i];
        if (ch >= MOTOR_COUNT || (used & (1U << ch))) return false;
        used |= 1U << ch; channels[i] = ch;
    }
    if (!dmaAllocate(MOTOR_DMA_ID, OWNER_MOTOR, 0)) return false;
    dmaEnable(MOTOR_DMA_ID);
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_TIMER1_FULL_REMAP, DISABLE);
    rcu_periph_clock_enable(RCU_TIMER1); timer_deinit(TIMER1);
    period = rcu_clock_freq_get(CK_APB1) * 2U / rate;
    timer_parameter_struct base; timer_struct_para_init(&base);
    base.prescaler = 0; base.period = period-1U;
    timer_init(TIMER1, &base);
    timer_oc_parameter_struct oc; timer_channel_output_struct_para_init(&oc);
    oc.outputstate = TIMER_CCX_ENABLE; oc.ocpolarity = TIMER_OC_POLARITY_HIGH;
    for (unsigned i = 0; i < MOTOR_COUNT; i++) {
        pins[i] = IOGetByTag(tags[channels[i]]); IOInit(pins[i], OWNER_MOTOR, i);
        IOLo(pins[i]); IOConfigGPIO(pins[i], IOCFG_AF_PP);
        timer_channel_output_config(TIMER1, channels[i], &oc);
        timer_channel_output_mode_config(TIMER1, channels[i], TIMER_OC_MODE_PWM0);
        timer_channel_output_shadow_config(TIMER1, channels[i], TIMER_OC_SHADOW_ENABLE);
    }
    timer_auto_reload_shadow_enable(TIMER1);
    timer_dma_transfer_config(TIMER1, TIMER_DMACFG_DMATA_CH0CV, TIMER_DMACFG_DMATC_4TRANSFER);
    dma_deinit(DMA0, DMA_CH1);
    dma_parameter_struct dma; dma_struct_para_init(&dma);
    dma.periph_addr = (uint32_t)&TIMER_DMATB(TIMER1);
    dma.periph_width = DMA_PERIPHERAL_WIDTH_16BIT;
    dma.memory_addr = (uint32_t)waveform; dma.memory_width = DMA_MEMORY_WIDTH_16BIT;
    dma.periph_inc = DMA_PERIPH_INCREASE_DISABLE; dma.memory_inc = DMA_MEMORY_INCREASE_ENABLE;
    dma.direction = DMA_MEMORY_TO_PERIPHERAL; dma.number = sizeof(waveform)/sizeof(uint16_t);
    dma.priority = DMA_PRIORITY_ULTRA_HIGH;
    dma_init(DMA0, DMA_CH1, &dma); dma_circulation_disable(DMA0, DMA_CH1);
    dma_memory_to_memory_disable(DMA0, DMA_CH1);
    dma_interrupt_enable(DMA0, DMA_CH1, DMA_INT_FTF | DMA_INT_ERR);
    dmaSetHandler(MOTOR_DMA_ID, motorDmaIrq, NVIC_PRIO_DSHOT_DMA, 0);
    memset(control, 0, sizeof(control));
    fault = false; active = false; initialized = true; stopHardware();
    dshotMotorCount = MOTOR_COUNT; dev->vTable = &vtable;
    return true;
}
