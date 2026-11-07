/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_ACCGYRO_QMI8658A
#include "common/utils.h"
#include "drivers/accgyro/accgyro.h"
#include "drivers/accgyro/accgyro_spi_qmi8658a.h"
#include "drivers/bus_spi.h"
#include "drivers/exti.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/system.h"
#include "drivers/time.h"

// QST QMI8658A Rev A, sections 5, 6 and 13.
enum {
    QMI_WHO_AM_I = 0x00,
    QMI_CTRL1 = 0x02,
    QMI_CTRL2 = 0x03,
    QMI_CTRL3 = 0x04,
    QMI_CTRL5 = 0x06,
    QMI_CTRL7 = 0x08,
    QMI_STATUSINT = 0x2d,
    QMI_TEMP_L = 0x33,
    QMI_RESET = 0x60,

    QMI_DEVICE_ID = 0x05,
    QMI_SOFT_RESET = 0x0b,
    QMI_ADDR_INCREMENT = 1 << 6,
    QMI_INT2_ENABLE = 1 << 4,
    QMI_ODR_7174_HZ = 0, // Maximum 6DOF ODR: 7174.4 Hz.
    QMI_ACC_16G = 3 << 4,
    QMI_GYRO_2048_DPS = 7 << 4,
    QMI_LPF_BOTH_260_HZ = (1 << 5) | (1 << 4) | (1 << 1) | 1,
    QMI_SYNC_SAMPLE = 1 << 7,
    QMI_ENABLE_GYRO_ACC = 3,
    QMI_SAMPLE_AVAILABLE = 1 << 0,
    QMI_SAMPLE_LOCKED = 1 << 1,
    QMI_STATUS_RESERVED = 0x7c,

    QMI_SAMPLE_BYTES = 14, // Temperature, XYZ acceleration and XYZ gyro.
    QMI_ACC_OFFSET = 2,
    QMI_GYRO_OFFSET = 8,
    QMI_LOCK_DELAY_US = 2,
    QMI_SPI_CLK_HZ = 7500000,
};

static bool qmiRead(const extDevice_t *dev, uint8_t reg, uint8_t *data, uint8_t len)
{
    const uint16_t errors = spiGetErrorCounter(dev);
    return spiReadRegMskBufRB(dev, reg, data, len) && spiGetErrorCounter(dev) == errors;
}

static bool qmiWriteConfig(const extDevice_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t readback;
    const uint16_t errors = spiGetErrorCounter(dev);
    return spiWriteRegRB(dev, reg, value) && spiGetErrorCounter(dev) == errors
        && qmiRead(dev, reg, &readback, 1) && readback == value;
}

static bool qmiReadSample(const extDevice_t *dev, uint8_t sample[QMI_SAMPLE_BYTES])
{
    uint8_t status;
    if (!qmiRead(dev, QMI_STATUSINT, &status, 1) || (status & QMI_STATUS_RESERVED)
        || !(status & (QMI_SAMPLE_AVAILABLE | QMI_SAMPLE_LOCKED))) {
        return false;
    }

    // Reading STATUSINT starts the lock. Reading through GZ_H releases it,
    // including when recovering a lock left by an interrupted SPI transfer.
    if (!(status & QMI_SAMPLE_LOCKED)) {
        delayMicroseconds(QMI_LOCK_DELAY_US);
    }
    return qmiRead(dev, QMI_TEMP_L, sample, QMI_SAMPLE_BYTES);
}

uint8_t qmi8658aSpiDetect(const extDevice_t *dev)
{
#ifdef USE_DMA
    spiDmaEnable(dev, false);
#endif
    uint8_t id;
    if (!qmiRead(dev, QMI_WHO_AM_I, &id, 1) || id != QMI_DEVICE_ID) {
        return MPU_NONE;
    }

    // Reset also releases any data lock left across an MCU-only restart.
    const uint16_t errors = spiGetErrorCounter(dev);
    if (!spiWriteRegRB(dev, QMI_RESET, QMI_SOFT_RESET) || spiGetErrorCounter(dev) != errors) {
        return MPU_NONE;
    }
    delay(20);
    if (!qmiRead(dev, QMI_WHO_AM_I, &id, 1) || id != QMI_DEVICE_ID
        || !qmiWriteConfig(dev, QMI_CTRL1, QMI_ADDR_INCREMENT | QMI_INT2_ENABLE)
        || !qmiWriteConfig(dev, QMI_CTRL2, QMI_ACC_16G | QMI_ODR_7174_HZ)
        || !qmiWriteConfig(dev, QMI_CTRL3, QMI_GYRO_2048_DPS | QMI_ODR_7174_HZ)
        || !qmiWriteConfig(dev, QMI_CTRL5, QMI_LPF_BOTH_260_HZ)
        || !qmiWriteConfig(dev, QMI_CTRL7, QMI_SYNC_SAMPLE | QMI_ENABLE_GYRO_ACC)) {
        return MPU_NONE;
    }

    spiSetClkDivisor(dev, spiCalculateDivider(QMI_SPI_CLK_HZ));
    delay(150);
    uint8_t sample[QMI_SAMPLE_BYTES];
    // Allow the first sample to arrive after the gyro's turn-on delay.
    for (unsigned attempt = 0; attempt < 50; attempt++) {
        if (qmiReadSample(dev, sample)) {
            return QMI8658A_SPI;
        }
        delay(1);
    }
    return MPU_NONE;
}

static int16_t qmiReadS16(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[1] << 8 | data[0]);
}

static void qmi8658aExtiHandler(extiCallbackRec_t *cb)
{
    gyroDev_t *gyro = container_of(cb, gyroDev_t, exti);
    gyro->gyroLastEXTI = getCycleCounter();
    // Keep a small phase margin between the data-ready edge and the task.
    gyro->gyroSyncEXTI = gyro->gyroLastEXTI + clockMicrosToCycles(12);
    gyro->detectedEXTI++;
}

static void qmi8658aGyroInit(gyroDev_t *gyro)
{
    gyro->dev.rxBuf[QMI_SAMPLE_BYTES] = 0;
    gyro->gyroModeSPI = GYRO_EXTI_NO_INT;
    if (gyro->mpuIntExtiTag != IO_TAG_NONE) {
        const IO_t int2 = IOGetByTag(gyro->mpuIntExtiTag);
        IOInit(int2, OWNER_GYRO_EXTI, 0);
        EXTIHandlerInit(&gyro->exti, qmi8658aExtiHandler);
        EXTIConfig(int2, &gyro->exti, NVIC_PRIO_MPU_INT_EXTI, IOCFG_IN_FLOATING, BETAFLIGHT_EXTI_TRIGGER_RISING);
        gyro->gyroSyncEXTI = getCycleCounter();
        gyro->gyroModeSPI = GYRO_EXTI_INT;
        EXTIEnable(int2);
    }
}

static bool qmi8658aGyroRead(gyroDev_t *gyro)
{
    uint8_t sample[QMI_SAMPLE_BYTES];
    if (!qmiReadSample(&gyro->dev, sample)) {
        return false;
    }

    for (unsigned axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        gyro->gyroADCRaw[axis] = qmiReadS16(&sample[QMI_GYRO_OFFSET + 2 * axis]);
    }
    // Share the same captured sample with the slower ACC and temperature tasks.
    memcpy(gyro->dev.rxBuf, sample, sizeof(sample));
    gyro->dev.rxBuf[QMI_SAMPLE_BYTES] = 1;
    return true;
}

static bool qmi8658aTemperatureRead(gyroDev_t *gyro, int16_t *temperature)
{
    if (!gyro->dev.rxBuf[QMI_SAMPLE_BYTES]) {
        return false;
    }
    *temperature = qmiReadS16(gyro->dev.rxBuf) / 256;
    return true;
}

static void qmi8658aAccInit(accDev_t *acc)
{
    acc->acc_1G = 2048; // +/-16 g in signed 16-bit samples.
}

static bool qmi8658aAccRead(accDev_t *acc)
{
    const uint8_t *sample = acc->gyro->dev.rxBuf;
    if (!sample[QMI_SAMPLE_BYTES]) {
        return false;
    }
    for (unsigned axis = 0; axis < XYZ_AXIS_COUNT; axis++) {
        acc->ADCRaw[axis] = qmiReadS16(&sample[QMI_ACC_OFFSET + 2 * axis]);
    }
    return true;
}

bool qmi8658aSpiGyroDetect(gyroDev_t *gyro)
{
    if (gyro->mpuDetectionResult.sensor != QMI8658A_SPI) {
        return false;
    }
    gyro->initFn = qmi8658aGyroInit;
    gyro->readFn = qmi8658aGyroRead;
    gyro->temperatureFn = qmi8658aTemperatureRead;
    gyro->scale = 1.0f / 16.0f;
    return true;
}

bool qmi8658aSpiAccDetect(accDev_t *acc)
{
    if (acc->mpuDetectionResult.sensor != QMI8658A_SPI) {
        return false;
    }
    acc->initFn = qmi8658aAccInit;
    acc->readFn = qmi8658aAccRead;
    return true;
}
#endif
