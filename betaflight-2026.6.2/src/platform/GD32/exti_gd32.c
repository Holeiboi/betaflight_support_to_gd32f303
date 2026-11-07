/* GPL-3.0-or-later. GD32 EXTI callback dispatch. */
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "drivers/io.h"
#include "drivers/exti.h"
#include "drivers/io_impl.h"
#include "platform/io_impl.h"
static extiCallbackRec_t *callbacks[16];
void EXTIInit(void) { rcu_periph_clock_enable(RCU_AF); }
void EXTIHandlerInit(extiCallbackRec_t *cb,extiHandlerCallback *fn) { cb->fn=fn; }
void EXTIConfig(IO_t io,extiCallbackRec_t *cb,int priority,ioConfig_t cfg,extiTrigger_t trigger) {
    const unsigned pin=IO_GPIOPinIdx(io); if(pin>=16) return;
    EXTIInit(); IOConfigGPIO(io,cfg); callbacks[pin]=cb;
    gpio_exti_source_select(IO_GPIOPortIdx(io),pin);
    exti_init(1U<<pin,EXTI_INTERRUPT,trigger==BETAFLIGHT_EXTI_TRIGGER_RISING ? EXTI_TRIG_RISING :
        trigger==BETAFLIGHT_EXTI_TRIGGER_FALLING ? EXTI_TRIG_FALLING : EXTI_TRIG_BOTH);
    exti_interrupt_disable(1U<<pin); exti_interrupt_flag_clear(1U<<pin);
    const IRQn_Type irq=pin<5 ? (IRQn_Type)(EXTI0_IRQn+pin) : pin<10 ? EXTI5_9_IRQn : EXTI10_15_IRQn;
    NVIC_SetPriority(irq,priority>>4); NVIC_EnableIRQ(irq);
}
void EXTIEnable(IO_t io) { exti_interrupt_flag_clear(IO_Pin(io)); exti_interrupt_enable(IO_Pin(io)); }
void EXTIDisable(IO_t io) { exti_interrupt_disable(IO_Pin(io)); }
void EXTIRelease(IO_t io) { EXTIDisable(io); callbacks[IO_GPIOPinIdx(io)]=0; }
static void dispatch(unsigned first,unsigned last) {
    for(unsigned i=first;i<=last;i++) if(exti_interrupt_flag_get(1U<<i)) {
        exti_interrupt_flag_clear(1U<<i); if(callbacks[i] && callbacks[i]->fn) callbacks[i]->fn(callbacks[i]);
    }
}
void EXTI0_IRQHandler(void) { dispatch(0,0); }
void EXTI1_IRQHandler(void) { dispatch(1,1); }
void EXTI2_IRQHandler(void) { dispatch(2,2); }
void EXTI3_IRQHandler(void) { dispatch(3,3); }
void EXTI4_IRQHandler(void) { dispatch(4,4); }
void EXTI5_9_IRQHandler(void) { dispatch(5,9); }
void EXTI10_15_IRQHandler(void) { dispatch(10,15); }
