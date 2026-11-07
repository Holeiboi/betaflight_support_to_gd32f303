/* GPL-3.0-or-later. Callbacks from the GD32 CDC class into Betaflight VCP. */
#pragma once
#include <stdint.h>
void gd32VcpReset(void);
void gd32VcpTxComplete(void);
void gd32VcpBaudChanged(uint32_t baud);
void gd32VcpCtrlChanged(uint16_t state);
