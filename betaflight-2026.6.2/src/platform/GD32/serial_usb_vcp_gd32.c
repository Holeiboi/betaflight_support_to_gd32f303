/* GPL-3.0-or-later. Betaflight serial stream over the GD32 USBD CDC device. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/serial.h"
#include "drivers/serial_usb_vcp.h"
#include "drivers/time.h"
#include "usb/cdc_acm_core.h"
#include "usbd_lld_int.h"
#include "usbd_lld_core.h"
#include "usb/vcp_gd32.h"

#define VCP_TX_SIZE 2048U
#define VCP_WRITE_TIMEOUT_US 50000U
static usb_dev device;
static serialPort_t port;
static uint8_t txQueue[VCP_TX_SIZE], txPacket[CDC_ACM_DATA_PACKET_SIZE];
static uint16_t txHead, txTail, rxAt;
static volatile bool txHold;
static uint16_t ctrlState;
static bool initialized, enumerated;
static volatile timeUs_t lastSof;
static void (*ctrlCallback)(void *, uint16_t);
static void *ctrlContext;
static void (*baudCallback)(serialPort_t *, uint32_t);
static serialPort_t *baudContext;

static uint32_t lockUsb(void)
{
    uint32_t previous = __get_PRIMASK();
    __disable_irq();
    return previous;
}

static void unlockUsb(uint32_t previous) { __set_PRIMASK(previous); }

static usb_cdc_handler *handler(void)
{
    return (usb_cdc_handler *)device.class_data[CDC_COM_INTERFACE];
}

static bool configured(void)
{
    return initialized && device.cur_status == USBD_CONFIGURED && handler();
}

static unsigned txUsed(void) { return (txHead - txTail) & (VCP_TX_SIZE - 1U); }

/* Caller serializes this with the USB interrupt. The staging buffer must stay
 * alive through completion, including the ZLP after a full 64-byte packet. */
static void kickTx(void)
{
    usb_cdc_handler *cdc = handler();
    if (txHold || !configured() || !cdc->packet_sent || txHead == txTail) return;
    unsigned length = 0;
    while (length < sizeof(txPacket) && txTail != txHead) {
        txPacket[length++] = txQueue[txTail];
        txTail = (txTail + 1U) & (VCP_TX_SIZE - 1U);
    }
    cdc->packet_sent = 0;
    usbd_ep_send(&device, CDC_IN_EP, txPacket, length);
}

/* These callbacks run in the USB ISR, with main-context queue access excluded. */
void gd32VcpReset(void)
{
    txHead = txTail = rxAt = ctrlState = 0;
    txHold = false;
    port.baudRate = 115200;
}

void gd32VcpTxComplete(void) { kickTx(); }

void gd32VcpCtrlChanged(uint16_t state)
{
    ctrlState = state;
    if (ctrlCallback) ctrlCallback(ctrlContext, state);
}

void gd32VcpBaudChanged(uint32_t baud)
{
    port.baudRate = baud;
    if (baudCallback) baudCallback(baudContext, baud);
}

void USBD_LP_CAN0_RX0_IRQHandler(void)
{
    const bool reset = (USBD_INTF & INTF_RSTIF) != 0;
    if (USBD_INTF & INTF_SOFIF) lastSof = microsISR();
    usbd_isr();
    if (reset) {
        gd32VcpReset();
        device.class_data[CDC_COM_INTERFACE] = NULL;
    }
    if (device.cur_status == USBD_CONFIGURED) enumerated = true;
}

static uint32_t rxWaiting(const serialPort_t *instance)
{
    (void)instance;
    uint32_t key = lockUsb();
    usb_cdc_handler *cdc = handler();
    uint32_t count = configured() && cdc->packet_receive ? cdc->receive_length - rxAt : 0;
    unlockUsb(key);
    return count;
}

static uint8_t readByte(serialPort_t *instance)
{
    (void)instance;
    uint8_t value = 0;
    uint32_t key = lockUsb();
    usb_cdc_handler *cdc = handler();
    if (configured() && cdc->packet_receive && rxAt < cdc->receive_length) {
        value = cdc->data[rxAt++];
        if (rxAt == cdc->receive_length) {
            rxAt = 0;
            /* Until the application consumes this packet, OUT stays NAK.
             * Host USB flow control prevents overwriting unread MSP bytes. */
            cdc_acm_data_receive(&device);
        }
    }
    unlockUsb(key);
    return value;
}

static uint32_t txFree(const serialPort_t *instance)
{
    (void)instance;
    uint32_t key = lockUsb();
    uint32_t available = VCP_TX_SIZE - 1U - txUsed();
    unlockUsb(key);
    return available;
}

static bool txEmpty(const serialPort_t *instance)
{
    (void)instance;
    uint32_t key = lockUsb();
    bool empty = !configured() || (txHead == txTail && handler()->packet_sent);
    unlockUsb(key);
    return empty;
}

static void writeBuffer(serialPort_t *instance, const void *data, int count)
{
    (void)instance;
    const uint8_t *bytes = data;
    const timeUs_t start = micros();
    while (count > 0) {
        uint32_t key = lockUsb();
        if (!configured()) {
            unlockUsb(key);
            return;
        }
        unsigned space = VCP_TX_SIZE - 1U - txUsed();
        unsigned length = (unsigned)count < space ? (unsigned)count : space;
        if (length > CDC_ACM_DATA_PACKET_SIZE) length = CDC_ACM_DATA_PACKET_SIZE;
        for (unsigned i = 0; i < length; i++) {
            txQueue[txHead] = *bytes++;
            txHead = (txHead + 1U) & (VCP_TX_SIZE - 1U);
        }
        kickTx();
        unlockUsb(key);
        count -= length;
        if (count && (__get_IPSR() || __get_PRIMASK() || __get_BASEPRI() ||
            (timeUs_t)(micros() - start) >= VCP_WRITE_TIMEOUT_US)) return;
    }
}

static void writeByte(serialPort_t *instance, uint8_t value) { writeBuffer(instance, &value, 1); }
/* MSP writes a frame as header, payload and CRC through three writeBuf
 * calls.  Without a hold, each call kicks a separate short USB packet and a
 * single reply costs several 1 ms frames.  Hold the queue until endWrite so
 * the whole reply leaves in one packet. */
static void beginWrite(serialPort_t *instance)
{
    (void)instance;
    uint32_t key = lockUsb();
    txHold = true;
    unlockUsb(key);
}
static void setBaud(serialPort_t *instance, uint32_t baud) { instance->baudRate = baud; }
static void setMode(serialPort_t *instance, portMode_e mode) { instance->mode = mode; }
static void setCtrlCallback(serialPort_t *instance, void (*cb)(void *, uint16_t), void *context)
{
    (void)instance;
    uint32_t key = lockUsb();
    ctrlCallback = cb;
    ctrlContext = context;
    unlockUsb(key);
}
static void setBaudCallback(serialPort_t *instance, void (*cb)(serialPort_t *, uint32_t), serialPort_t *context)
{
    (void)instance;
    uint32_t key = lockUsb();
    baudCallback = cb;
    baudContext = context;
    unlockUsb(key);
}
static void endWrite(serialPort_t *instance)
{
    (void)instance;
    uint32_t key = lockUsb();
    txHold = false;
    kickTx();
    unlockUsb(key);
}

static const struct serialPortVTable vtable = {
    .serialWrite = writeByte,
    .serialTotalRxWaiting = rxWaiting,
    .serialTotalTxFree = txFree,
    .serialRead = readByte,
    .serialSetBaudRate = setBaud,
    .isSerialTransmitBufferEmpty = txEmpty,
    .setMode = setMode,
    .setCtrlLineStateCb = setCtrlCallback,
    .setBaudRateCb = setBaudCallback,
    .writeBuf = writeBuffer,
    .beginWrite = beginWrite,
    .endWrite = endWrite,
};

void usbVcpInit(void)
{
    if (initialized || SystemCoreClock != 120000000U) return;
    port.vTable = &vtable;
    port.mode = MODE_RXTX;
    port.baudRate = 115200;
    const ioTag_t tags[] = {IO_TAG(PA8), IO_TAG(PA11), IO_TAG(PA12)};
    for (unsigned i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        IO_t io = IOGetByTag(tags[i]);
        if (IOGetOwner(io) != OWNER_FREE) return;
    }
    for (unsigned i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        IO_t io = IOGetByTag(tags[i]);
        IOInit(io, OWNER_USB, i);
        IOLo(io);
        IOConfigGPIO(io, i == 0 ? IOCFG_OUT_PP : IOCFG_IN_FLOATING);
    }
    /* PA8 drives the schematic's external D+ pull-up; keep it low long enough
     * for a host to see a disconnect when restarting a powered board. */
    delay(30);
    rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
    rcu_periph_clock_enable(RCU_USBD);
    rcu_periph_reset_enable(RCU_USBDRST);
    rcu_periph_reset_disable(RCU_USBDRST);
    usbd_init(&device, &cdc_desc, &cdc_class);
    initialized = true;
    NVIC_ClearPendingIRQ(USBD_LP_CAN0_RX0_IRQn);
    NVIC_SetPriority(USBD_LP_CAN0_RX0_IRQn, NVIC_PRIO_USB >> 4);
    NVIC_EnableIRQ(USBD_LP_CAN0_RX0_IRQn);
    usbd_connect(&device);
}

serialPort_t *usbVcpOpen(void) { return initialized ? &port : NULL; }
uint32_t usbVcpGetBaudRate(serialPort_t *instance) { return instance->baudRate; }
uint8_t usbVcpIsConnected(void) { return enumerated; }
uint8_t usbVcpIsActive(void)
{
    // Snapshot the ISR timestamp BEFORE reading now: an intervening SOF
    // must not make lastSof newer than now and underflow the unsigned age.
    const timeUs_t frame = lastSof;
    return configured() && (timeUs_t)(micros() - frame) < 10000U;
}
