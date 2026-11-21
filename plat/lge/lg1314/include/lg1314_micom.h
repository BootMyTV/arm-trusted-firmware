/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LG1314_MICOM_H
#define LG1314_MICOM_H

#include <stddef.h>
#include <stdint.h>

/* Polled I2C write of buf[0..len) to the standby MICOM; 0 or -errno. */
int lg1314_micom_write(const uint8_t *buf, size_t len);

#endif /* LG1314_MICOM_H */
