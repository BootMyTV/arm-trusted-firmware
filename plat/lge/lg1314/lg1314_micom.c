/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal polled writer for the I2C controller the standby MICOM hangs off.
 * Only used on the way out (PSCI SYSTEM_OFF), after the OS has stopped every
 * other CPU and will not touch the bus again, so the controller is simply
 * reset and reprogrammed here rather than shared.
 *
 * Register-level sequence as in the kernel's i2c-lg1314 driver: each
 * low-level transfer moves up to 4 bytes through MDR; a write is an address
 * phase (START) followed by data phases, the last one with STOP.
 */

#include <errno.h>

#include <platform_def.h>

#include <common/debug.h>
#include <drivers/delay_timer.h>
#include <lib/mmio.h>

#include <lg1314_micom.h>

#define I2C_MDR			0x00
#define I2C_CTR0		0x0c
#define I2C_CTR1		0x10
#define I2C_MSR			0x14
#define I2C_CPHR		0x20
#define I2C_CPLR		0x24

#define I2C_CTR0_TGO		0x01
#define I2C_CTR0_IACK		0x02
#define I2C_CTR0_NTB(n)		(((n) - 1) << 3)
#define I2C_CTR0_STO		0x40
#define I2C_CTR0_STA		0x80

#define I2C_CTR1_SWR		0x02
#define I2C_CTR1_CMF		0x08
#define I2C_CTR1_IEN		0x10
#define I2C_CTR1_CEN		0x20

#define I2C_MSR_SNB		0x02
#define I2C_MSR_MIS		0x04

#define I2C_FIFO_SIZE		4
#define I2C_XFER_TIMEOUT_US	10000

static int i2c_xfer(uintptr_t base, const uint8_t *buf, size_t len,
		    uint8_t flags)
{
	uint64_t timeout;
	size_t i;

	mmio_write_8(base + I2C_CTR1, I2C_CTR1_CMF | I2C_CTR1_IEN | I2C_CTR1_CEN);
	for (i = 0; i < len; i++)
		mmio_write_8(base + I2C_MDR, buf[i]);
	mmio_write_8(base + I2C_CTR0, I2C_CTR0_TGO | I2C_CTR0_NTB(len) | flags);

	timeout = timeout_init_us(I2C_XFER_TIMEOUT_US);
	while ((mmio_read_8(base + I2C_MSR) & I2C_MSR_MIS) == 0U) {
		if (timeout_elapsed(timeout))
			return -ETIMEDOUT;
	}
	mmio_write_8(base + I2C_CTR0, I2C_CTR0_IACK);

	if ((mmio_read_8(base + I2C_MSR) & I2C_MSR_SNB) != 0U)
		return -EIO;	/* slave NAK */

	return 0;
}

int lg1314_micom_write(const uint8_t *buf, size_t len)
{
	const uintptr_t base = LG1314_MICOM_I2C_BASE;
	/* Bus rate trimmed by 10% as in the kernel driver (measured fast). */
	const uint32_t rate = LG1314_MICOM_I2C_HZ - LG1314_MICOM_I2C_HZ / 10U;
	const uint32_t div = (LG1314_BUSCLK_HZ + rate * 10U - 1U) / (rate * 10U);
	uint8_t addr = (uint8_t)(LG1314_MICOM_I2C_ADDR << 1);
	size_t off, n;
	int ret;

	if (len == 0U)
		return -EINVAL;

	/* Known state, whatever the OS left behind. */
	mmio_write_8(base + I2C_CTR1, I2C_CTR1_SWR);
	mdelay(1);
	mmio_write_8(base + I2C_CTR1, 0);
	mmio_write_8(base + I2C_CPHR, (uint8_t)(div >> 8));
	mmio_write_8(base + I2C_CPLR, (uint8_t)div);

	ret = i2c_xfer(base, &addr, 1, I2C_CTR0_STA);
	for (off = 0; ret == 0 && off < len; off += n) {
		n = len - off;
		if (n > I2C_FIFO_SIZE)
			n = I2C_FIFO_SIZE;
		ret = i2c_xfer(base, &buf[off], n,
			       off + n == len ? I2C_CTR0_STO : 0U);
	}

	if (ret != 0) {
		/* Leave the controller idle rather than mid-transfer. */
		mmio_write_8(base + I2C_CTR1, I2C_CTR1_SWR);
		mdelay(1);
		mmio_write_8(base + I2C_CTR1, 0);
	}
	return ret;
}
