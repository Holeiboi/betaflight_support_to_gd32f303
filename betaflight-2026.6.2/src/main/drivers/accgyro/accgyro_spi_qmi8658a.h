/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define QMI8658A_SAMPLE_RATE_HZ 7174

struct extDevice_s;
struct gyroDev_s;
struct accDev_s;

uint8_t qmi8658aSpiDetect(const struct extDevice_s *dev);
bool qmi8658aSpiGyroDetect(struct gyroDev_s *gyro);
bool qmi8658aSpiAccDetect(struct accDev_s *acc);
