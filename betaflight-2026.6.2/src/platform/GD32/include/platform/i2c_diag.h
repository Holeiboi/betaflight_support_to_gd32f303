/* GPL-3.0-or-later. Diagnostics for PB6/PB7. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t transfers, recoveries, sclTimeouts, sdaBusy, addressNacks, dataNacks;
    uint8_t lastFailure, lastAddress, lastRegister, sclHigh, sdaHigh;
    bool configured;
} gd32I2cDiagnostics_t;
void gd32I2cGetDiagnostics(gd32I2cDiagnostics_t *result);
const char *gd32I2cFailureName(uint8_t failure);
