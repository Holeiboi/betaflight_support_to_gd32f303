/* GPL-3.0-or-later. Four standard PWM ESC outputs on TIMER1 CH0..3. */
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "platform.h"
#include "drivers/io.h"
#include "drivers/pwm_output.h"
static uint8_t count;
static IO_t pins[4];
static uint8_t channels[4];
static bool active;
static float fromExternal(uint16_t v) { return v; }
static uint16_t toExternal(float v) { return v; }
static void noOp(void) {}
static bool enable(void) { active=count==4; return active; }
static void disable(void) {
    active=false;
    for (unsigned i=0;i<count;i++) timer_channel_output_pulse_value_config(TIMER1,i,0);
    timer_event_software_generate(TIMER1,TIMER_EVENT_SRC_UPG);
}
static bool enabled(unsigned i) { return active && i<count; }
static IO_t getIO(unsigned i) { return i<count ? pins[i] : IO_NONE; }
static void writeMotor(uint8_t i,float v) {
    if (i>=count || !active) return;
    const uint16_t pulse=isfinite(v) ? (uint16_t)fminf(2000.0f,fmaxf(1000.0f,v)) : 1000;
    timer_channel_output_pulse_value_config(TIMER1,channels[i],pulse);
}
static void writeInt(uint8_t i,uint16_t v) { writeMotor(i,v); }
static void releaseIO(unsigned i) {
    if (i>=count || active) return;
    timer_channel_output_state_config(TIMER1,channels[i],TIMER_CCX_DISABLE);
    IOConfigGPIO(pins[i],IOCFG_IPU);
}
static bool reinstateIO(unsigned i) {
    if (i>=count || active) return false;
    IOLo(pins[i]); IOConfigGPIO(pins[i],IOCFG_AF_PP);
    timer_channel_output_state_config(TIMER1,channels[i],TIMER_CCX_ENABLE);
    return true;
}
static const motorVTable_t vtable={
    .postInit=noOp,.convertExternalToMotor=fromExternal,.convertMotorToExternal=toExternal,
    .enable=enable,.disable=disable,.isMotorEnabled=enabled,.write=writeMotor,
    .writeInt=writeInt,.updateComplete=noOp,.shutdown=disable,.getMotorIO=getIO,
    .releaseMotorIO=releaseIO,.reinstateMotorIO=reinstateIO
};
bool motorPwmDevInit(motorDevice_t *dev,const motorDevConfig_t *cfg,uint16_t idlePulse) {
    (void)idlePulse;
    const ioTag_t tags[4]={IO_TAG(PA0),IO_TAG(PA1),IO_TAG(PA2),IO_TAG(PA3)};
    if (dev->count!=4 || cfg->motorProtocol!=MOTOR_PROTOCOL_PWM ||
        cfg->motorInversion || cfg->motorPwmRate<50 || cfg->motorPwmRate>400 || SystemCoreClock!=120000000U) return false;
    for(unsigned i=0;i<4;i++) if(cfg->ioTags[i]!=tags[i] || IOGetOwner(IOGetByTag(tags[i]))!=OWNER_FREE) return false;
    unsigned used=0;
    for(unsigned i=0;i<4;i++) {
        const unsigned ch=cfg->motorOutputReordering[i];
        if(ch>=4 || (used & (1U<<ch))) return false;
        used|=1U<<ch; channels[i]=ch;
    }
    rcu_periph_clock_enable(RCU_TIMER1); timer_deinit(TIMER1);
    timer_parameter_struct base; timer_struct_para_init(&base);
    /* APB1 is AHB/2; TIMER1 receives twice APB1 (120 MHz). */
    base.prescaler=rcu_clock_freq_get(CK_APB1)*2U/1000000U-1U;
    base.period=1000000/cfg->motorPwmRate-1;
    timer_init(TIMER1,&base);
    timer_oc_parameter_struct oc; timer_channel_output_struct_para_init(&oc);
    oc.outputstate=TIMER_CCX_ENABLE; oc.ocpolarity=TIMER_OC_POLARITY_HIGH;
    count=4; active=false;
    for(unsigned i=0;i<4;i++) {
        pins[i]=IOGetByTag(tags[channels[i]]); IOInit(pins[i],OWNER_MOTOR,i);
        IOLo(pins[i]); IOConfigGPIO(pins[i],IOCFG_AF_PP);
        timer_channel_output_config(TIMER1,channels[i],&oc);
        timer_channel_output_mode_config(TIMER1,channels[i],TIMER_OC_MODE_PWM0);
        timer_channel_output_pulse_value_config(TIMER1,channels[i],0);
        timer_channel_output_shadow_config(TIMER1,channels[i],TIMER_OC_SHADOW_ENABLE);
    }
    timer_auto_reload_shadow_enable(TIMER1);
    timer_event_software_generate(TIMER1,TIMER_EVENT_SRC_UPG); timer_enable(TIMER1);
    dev->vTable=&vtable; return true;
}

/*
 * BEEPER_PWM_HZ is 0 on this board (GPIO-driven active buzzer), so the PWM
 * beeper path is never taken.  The shared sound_beeper code still references
 * these symbols when USE_PWM_OUTPUT is enabled.
 */
void beeperPwmInit(ioTag_t tag, uint16_t frequency) { (void)tag; (void)frequency; }
void pwmWriteBeeper(bool onoff) { (void)onoff; }
