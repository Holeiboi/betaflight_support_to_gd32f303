/* GPL-3.0-or-later. Hardware I2C0 on PB6/PB7 or PB8/PB9, 400 kHz, bounded transfers. */
#include <stdbool.h>
#include <stdint.h>
#include "platform.h"
#include "drivers/bus_i2c.h"
#include "drivers/io.h"
#include "drivers/time.h"
#include "pg/bus_i2c.h"
#include "platform/i2c_diag.h"

enum { I2C_OK, I2C_BAD_CONFIG, I2C_SCL_TIMEOUT, I2C_SDA_BUSY,
       I2C_ADDRESS_NACK, I2C_REGISTER_NACK, I2C_DATA_NACK,
       I2C_BUS_ERROR, I2C_ARBITRATION_LOST, I2C_OVERRUN, I2C_TRANSFER_TIMEOUT };
static gd32I2cDiagnostics_t diagnostics;
static IO_t scl, sda;
static bool configured, remapped, online, failed;
static uint16_t errors;
static uint32_t started, timeoutCycles;
static timeUs_t retryAt;

static uint32_t lockBus(void) {
    const uint32_t key = __get_PRIMASK();
    __disable_irq();
    return key;
}
static void unlockBus(uint32_t key) { __set_PRIMASK(key); }

static bool expired(void) {
    if ((uint32_t)(DWT->CYCCNT - started) < timeoutCycles) return false;
    diagnostics.lastFailure = I2C_TRANSFER_TIMEOUT;
    if (!IORead(scl)) { diagnostics.sclTimeouts++; diagnostics.lastFailure = I2C_SCL_TIMEOUT; }
    return true;
}

/* Never read STAT1 here: STAT0 followed by STAT1 clears ADDSEND, which
 * must happen only after configuring ACK/POS for the receive length. */
static bool waitFlag(uint32_t mask, uint8_t nackReason) {
    for (;;) {
        const uint32_t status = I2C_STAT0(I2C0);
        if (status & (I2C_STAT0_AERR | I2C_STAT0_BERR | I2C_STAT0_LOSTARB | I2C_STAT0_OUERR)) {
            if (status & I2C_STAT0_AERR) {
                diagnostics.lastFailure = nackReason;
                if (nackReason == I2C_ADDRESS_NACK) diagnostics.addressNacks++;
                else diagnostics.dataNacks++;
            } else if (status & I2C_STAT0_BERR) diagnostics.lastFailure = I2C_BUS_ERROR;
            else if (status & I2C_STAT0_LOSTARB) diagnostics.lastFailure = I2C_ARBITRATION_LOST;
            else diagnostics.lastFailure = I2C_OVERRUN;
            return false;
        }
        if (status & mask) return true;
        if (expired()) return false;
    }
}

static void clearAddress(void) {
    (void)I2C_STAT0(I2C0);
    (void)I2C_STAT1(I2C0);
}

/* GPIO clocks are used only to free a stuck slave at initialization/error
 * recovery. Every address, register and payload byte uses hardware I2C0. */
static bool releaseBus(void) {
    IOHi(scl); IOHi(sda);
    IOConfigGPIO(scl, IOCFG_OUT_OD); IOConfigGPIO(sda, IOCFG_OUT_OD);
    IOHi(scl); IOHi(sda);
    delayMicroseconds(5);
    for (unsigned i = 0; i < 9 && IORead(scl) && !IORead(sda); i++) {
        IOLo(scl); delayMicroseconds(5); IOHi(scl); delayMicroseconds(5);
    }
    if (!IORead(scl)) { diagnostics.sclTimeouts++; diagnostics.lastFailure = I2C_SCL_TIMEOUT; return false; }
    IOLo(scl); IOLo(sda); delayMicroseconds(5);
    IOHi(scl); delayMicroseconds(5); IOHi(sda); delayMicroseconds(5);
    if (!IORead(sda)) { diagnostics.sdaBusy++; diagnostics.lastFailure = I2C_SDA_BUSY; return false; }
    return true;
}

void i2cPinConfigure(const i2cConfig_t *config) {
    remapped = config[0].ioTagScl == IO_TAG(PB8) && config[0].ioTagSda == IO_TAG(PB9);
    configured = remapped || (config[0].ioTagScl == IO_TAG(PB6) && config[0].ioTagSda == IO_TAG(PB7));
    scl = IOGetByTag(config[0].ioTagScl); sda = IOGetByTag(config[0].ioTagSda);
    diagnostics.configured = configured;
}

void i2cInit(i2cDevice_e device) {
    if (device != I2CDEV_1 || !configured) return;
    online = false;
    timeoutCycles = SystemCoreClock / 1000U; // 1 ms for the entire transaction
    diagnostics.recoveries++;
    IOInit(scl, OWNER_I2C_SCL, 0); IOInit(sda, OWNER_I2C_SDA, 0);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_I2C0);
    i2c_deinit(I2C0);
    if (!releaseBus()) { retryAt = micros() + 10000U; return; }
    gpio_pin_remap_config(GPIO_I2C0_REMAP, remapped ? ENABLE : DISABLE);
    IOConfigGPIO(scl, IOCFG_AF_OD); IOConfigGPIO(sda, IOCFG_AF_OD);
    i2c_clock_config(I2C0, 400000U, I2C_DTCY_2);
    i2c_mode_addr_config(I2C0, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0);
    i2c_enable(I2C0);
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);
    online = true;
}

static bool address(uint8_t addr, bool read) {
    i2c_start_on_bus(I2C0);
    if (!waitFlag(I2C_STAT0_SBSEND, I2C_ADDRESS_NACK)) return false;
    i2c_master_addressing(I2C0, addr << 1, read ? I2C_RECEIVER : I2C_TRANSMITTER);
    return waitFlag(I2C_STAT0_ADDSEND, I2C_ADDRESS_NACK);
}

static bool receive(uint8_t *buf, unsigned len) {
    if (len == 1) {
        const uint32_t key = lockBus();
        i2c_ack_config(I2C0, I2C_ACK_DISABLE);
        clearAddress();
        i2c_stop_on_bus(I2C0);
        unlockBus(key);
        if (!waitFlag(I2C_STAT0_RBNE, I2C_DATA_NACK)) return false;
        *buf = i2c_data_receive(I2C0);
        return true;
    }
    if (len == 2) {
        const uint32_t key = lockBus();
        clearAddress();
        i2c_ack_config(I2C0, I2C_ACK_DISABLE);
        unlockBus(key);
    } else {
        clearAddress();
        while (len > 3) {
            if (!waitFlag(I2C_STAT0_RBNE, I2C_DATA_NACK)) return false;
            *buf++ = i2c_data_receive(I2C0); len--;
        }
        if (!waitFlag(I2C_STAT0_BTC, I2C_DATA_NACK)) return false;
        const uint32_t key = lockBus();
        i2c_ack_config(I2C0, I2C_ACK_DISABLE);
        *buf++ = i2c_data_receive(I2C0);
        unlockBus(key);
    }
    // BTC holds SCL low with both final bytes buffered; only this short
    // STOP/read sequence excludes IRQs, never the wire transfer itself.
    if (!waitFlag(I2C_STAT0_BTC, I2C_DATA_NACK)) return false;
    const uint32_t key = lockBus();
    i2c_stop_on_bus(I2C0);
    *buf++ = i2c_data_receive(I2C0);
    *buf = i2c_data_receive(I2C0);
    unlockBus(key);
    return true;
}

static bool transfer(i2cDevice_e dev, uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf, bool read) {
    if (dev != I2CDEV_1 || !configured || !buf || !len || addr > 0x7f) {
        diagnostics.lastFailure = I2C_BAD_CONFIG; failed = true; errors++; return false;
    }
    if (!online) {
        // A disconnected sensor must not monopolize every gyro period.
        if ((int32_t)(micros() - retryAt) < 0) return false;
        i2cInit(dev);
        if (!online) { failed = true; errors++; return false; }
    }
    diagnostics.transfers++;
    // Allow long diagnostic reads too, while keeping six-byte IMU failures
    // bounded to 1 ms. Normal 400 kHz IMU transfers take about 215 us.
    const uint32_t budgetUs = 250U + 25U * (len + 3U);
    timeoutCycles = (SystemCoreClock / 1000000U) * (budgetUs > 1000U ? budgetUs : 1000U);
    started = DWT->CYCCNT;
    while (I2C_STAT1(I2C0) & I2C_STAT1_I2CBSY) { if (expired()) goto error; }
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);
    i2c_ackpos_config(I2C0, I2C_ACKPOS_CURRENT);
    if (!address(addr, false)) goto error;
    clearAddress();
    i2c_data_transmit(I2C0, reg);
    if (!waitFlag(I2C_STAT0_BTC, I2C_REGISTER_NACK)) goto error;
    if (read) {
        if (len == 2) i2c_ackpos_config(I2C0, I2C_ACKPOS_NEXT);
        if (!address(addr, true) || !receive(buf, len)) goto error;
    } else {
        for (unsigned i = 0; i < len; i++) {
            i2c_data_transmit(I2C0, buf[i]);
            if (!waitFlag(I2C_STAT0_BTC, I2C_DATA_NACK)) goto error;
        }
        i2c_stop_on_bus(I2C0);
    }
    while (I2C_CTL0(I2C0) & I2C_CTL0_STOP) { if (expired()) goto error; }
    i2c_ackpos_config(I2C0, I2C_ACKPOS_CURRENT);
    i2c_ack_config(I2C0, I2C_ACK_ENABLE);
    failed = false;
    return true;
error:
    diagnostics.lastAddress = addr; diagnostics.lastRegister = reg;
    errors++; failed = true;
    i2c_stop_on_bus(I2C0);
    i2c_disable(I2C0);
    online = false; retryAt = micros() + 10000U;
    return false;
}

bool i2cWriteBuffer(i2cDevice_e d,uint8_t a,uint8_t r,uint8_t n,uint8_t *b) { return transfer(d,a,r,n,b,false); }
bool i2cWrite(i2cDevice_e d,uint8_t a,uint8_t r,uint8_t b) { return transfer(d,a,r,1,&b,false); }
bool i2cRead(i2cDevice_e d,uint8_t a,uint8_t r,uint8_t n,uint8_t *b) { return transfer(d,a,r,n,b,true); }
bool i2cReadBuffer(i2cDevice_e d,uint8_t a,uint8_t r,uint8_t n,uint8_t *b) { return i2cRead(d,a,r,n,b); }
bool i2cBusy(i2cDevice_e d,bool *error) { if(error) *error=failed || d!=I2CDEV_1 || !configured; return false; }
uint16_t i2cGetErrorCounter(void) { return errors; }
void gd32I2cGetDiagnostics(gd32I2cDiagnostics_t *result) {
    *result=diagnostics;
    result->sclHigh=configured && IORead(scl); result->sdaHigh=configured && IORead(sda);
}
const char *gd32I2cFailureName(uint8_t failure) {
    static const char * const names[]={"NONE","BAD_CONFIG","SCL_TIMEOUT","SDA_BUSY",
        "ADDRESS_NACK","REGISTER_NACK","DATA_NACK","BUS_ERROR","ARBITRATION_LOST","OVERRUN","TRANSFER_TIMEOUT"};
    return failure<sizeof(names)/sizeof(names[0]) ? names[failure] : "UNKNOWN";
}
