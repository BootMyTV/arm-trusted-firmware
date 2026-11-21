/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * TF-A BL31 for the LG1314 (M19A1) SoC, 4x Cortex-A53.
 * Entered RESET_TO_BL31 by the prior boot loader after it has done all SoC
 * init (DDR/clocks/display/GPU). BL31 provides PSCI + a null secure world
 * (everything opened to non-secure) and hands off to a preloaded BL33 (kernel).
 */

#ifndef PLATFORM_DEF_H
#define PLATFORM_DEF_H

#include <arch.h>
#include <lib/utils_def.h>
#include <plat/common/common_def.h>

/* -------------------------------------------------------------------------
 * Topology: 1 cluster, 4 cores
 * ---------------------------------------------------------------------- */
#define PLATFORM_CLUSTER_COUNT		U(1)
#define PLATFORM_CLUSTER0_CORE_COUNT	U(4)
#define PLATFORM_CORE_COUNT		PLATFORM_CLUSTER0_CORE_COUNT

#define PLAT_MAX_PWR_LVL		U(1)	/* cluster + core */
#define PLAT_NUM_PWR_DOMAINS		(PLATFORM_CLUSTER_COUNT + \
					 PLATFORM_CORE_COUNT)
#define PLAT_MAX_RET_STATE		U(1)
#define PLAT_MAX_OFF_STATE		U(2)

/* Primary CPU: MPIDR Aff1=0 (cluster), Aff0=0 (core) */
#define LG1314_PRIMARY_CPU		U(0x0)

/* -------------------------------------------------------------------------
 * Memory map
 *
 * The 0x3e400000..0x4f400000 gap is NOT handed to Linux (it sits between the
 * two low DRAM banks), so it's a safe home for BL31 that the non-secure OS
 * does not map.
 * ---------------------------------------------------------------------- */
#define LG1314_SEC_BASE			UL(0x3e400000)
#define LG1314_SEC_SIZE			UL(0x00200000)	/* 2 MiB */

/*
 * Bootrom per-core spin mailbox (0xfd3002dc, inside the 0xfd300000 ctrl-regs
 * block): a secondary coming out of reset sets CPUECTLR.SMPEN then spins
 * reading this word. plat_get_my_entrypoint() reads it to distinguish a cold
 * from a warm boot.
 */
#define PLAT_LG1314_TRUSTED_MAILBOX_BASE	UL(0xfd3002dc)

/*
 * CPUPWR block: per-core rail power-up/down + SMPEN status, in the 0xfd300000
 * ctrl-regs page (same page as the bootrom CPU mailbox at +0x2dc). Offsets and
 * per-cpu lanes (used only on the truly-off cold-start fallback):
 *   +0x3e4 core-enable bit (cpu1->b1, cpu2->b2, cpu3->b3)
 *   +0x3e8 8-step power-switch ramp (lane byte per cpu)  +0x3f4 ramp ack (poll)
 *   +0x3ec/+0x3f0 16-step clamp/isolation release
 *   +0x3f8 [12:15] per-cpu power-up request   +0x3fc [0:3] SMPEN/up status
 *   +0x408 [8:11] post-up latch
 */
#define PLAT_LG1314_CPUPWR_BASE		UL(0xfd300000)
#define CPUPWR_CORE_ENABLE		U(0x3e4)
#define CPUPWR_RAMP			U(0x3e8)
#define CPUPWR_CLAMP0			U(0x3ec)
#define CPUPWR_CLAMP1			U(0x3f0)
#define CPUPWR_RAMP_ACK			U(0x3f4)
#define CPUPWR_RAMP_REQ			U(0x3f8)	/* [12:15] per-cpu */
#define CPUPWR_STATUS			U(0x3fc)	/* [0:3] SMPEN per cpu */
#define CPUPWR_POST_LATCH		U(0x408)	/* [8:11] per-cpu */

#define BL31_BASE			UL(0x3e440000)
#define BL31_LIMIT			UL(0x3e500000)	/* 768 KiB */

/*
 * Non-secure (BL33) image: the kernel Image, preloaded by the boot loader.
 * Built with PRELOADED_BL33_BASE=<this>; plat_get_ns_image_entrypoint()
 * returns it. The DTB phys is passed to BL31 in x0 (arg0), else this default.
 */
#ifndef PLAT_LG1314_NS_IMAGE_BASE
#define PLAT_LG1314_NS_IMAGE_BASE	UL(0x00200000)
#endif
#ifndef PLAT_LG1314_DTB_BASE
#define PLAT_LG1314_DTB_BASE		UL(0x02000000)
#endif

/* DRAM aperture to map for the handoff (covers all three Linux banks). */
#define LG1314_DRAM_BASE		UL(0x00000000)
#define LG1314_DRAM_SIZE		UL(0xc0000000)	/* up to 0xc0000000 */

/* -------------------------------------------------------------------------
 * Devices
 * ---------------------------------------------------------------------- */
/* GIC-400 (GICv2): distributor + CPU interface (from the kernel DT). */
#define LG1314_GICD_BASE		UL(0xc0001000)
#define LG1314_GICC_BASE		UL(0xc0002000)

/* PL011 UART0 (serial console; the prior boot loader already configured it). */
#define PLAT_LG1314_UART_BASE		UL(0xfe000000)
#define PLAT_LG1314_UART_CLK_IN_HZ	U(198000000)	/* busclk = xtal 24MHz * 33/4 */
#define PLAT_LG1314_UART_BAUDRATE	U(115200)

/* APB bus clock feeding the UART, I2C and watchdog blocks. */
#define LG1314_BUSCLK_HZ		U(198000000)

/*
 * SP805 watchdog (the kernel's watchdog@fd200000), used for PSCI SYSTEM_RESET.
 * Resets the SoC; the standby MICOM, and the RTC it keeps, stay up.
 */
#define LG1314_SP805_BASE		UL(0xfd200000)
/* Reset fires on the second expiry; a few us at busclk. */
#define LG1314_SP805_RESET_TICKS	U(0x1000)

/*
 * Standby MICOM on I2C bus 1 (the kernel's i2c@fe400000, client 0x29), used
 * for PSCI SYSTEM_OFF: command 0x25 = enter power state, state 0 = cold
 * standby. The second argument is the power-off reason the MICOM records.
 */
#define LG1314_MICOM_I2C_BASE		UL(0xfe400000)
#define LG1314_MICOM_I2C_ADDR		U(0x29)
#define LG1314_MICOM_I2C_HZ		U(100000)
#define LG1314_MICOM_CMD_POWER		U(0x25)
#define LG1314_MICOM_POWER_COLD_STANDBY	U(0x00)
#define LG1314_MICOM_OFF_BY_CPUCMD	U(0x00)
/*
 * Command 0x21 sets the power-on reason the MICOM reports to the next boot. Set
 * on reset, so the boot after a reboot doesn't see the stale reason of the last
 * real power-on (e.g. "mains restored"); 0x20 = "remote key", an ordinary
 * power-on to anything reading it (bootmytv shim, webOS).
 */
#define LG1314_MICOM_CMD_POWER_ON_MODE	U(0x21)
#define LG1314_MICOM_ON_BY_REMOTE_KEY	U(0x20)

/*
 * TZC-400 DDR firewalls: two instances (memory controllers M0/M1).
 * We open both region-0 (background) fully to the non-secure world - null
 * secure world, REE owns all DRAM.
 */
#define LG1314_TZASC0_BASE		UL(0xc9900000)	/* M0 */
#define LG1314_TZASC1_BASE		UL(0xc9910000)	/* M1 */

/* Arch generic timer input clock (DT: 24 MHz). */
#define LG1314_SYS_CNT_FREQ		U(24000000)

/* Device MMIO aperture to map (UART/GIC/TZC all live below 0x100000000). */
#define LG1314_DEVICE_BASE		UL(0xc0000000)
#define LG1314_DEVICE_SIZE		UL(0x40000000)	/* 0xc0000000..0x100000000 */

/* -------------------------------------------------------------------------
 * TF-A framework sizing
 * ---------------------------------------------------------------------- */
#define PLATFORM_STACK_SIZE		U(0x1000)

#define CACHE_WRITEBACK_SHIFT		6
#define CACHE_WRITEBACK_GRANULE		(U(1) << CACHE_WRITEBACK_SHIFT)

#define MAX_MMAP_REGIONS		U(8)
#define MAX_XLAT_TABLES			U(8)

#define PLAT_PHY_ADDR_SPACE_SIZE	(ULL(1) << 32)
#define PLAT_VIRT_ADDR_SPACE_SIZE	(ULL(1) << 32)

#define PLAT_LG1314_UART_CLK		PLAT_LG1314_UART_CLK_IN_HZ

#endif /* PLATFORM_DEF_H */
