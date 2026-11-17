/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <arch.h>
#include <asm_macros.S>
#include <platform_def.h>

	.globl	plat_my_core_pos
	.globl	plat_lg1314_calc_core_pos
	.globl	plat_is_my_cpu_primary
	.globl	plat_get_my_entrypoint
	.globl	plat_secondary_cold_boot_setup
	.globl	plat_crash_console_init
	.globl	plat_crash_console_putc
	.globl	plat_crash_console_flush
	.globl	platform_mem_init

	/* ---------------------------------------------------------------
	 * unsigned int plat_my_core_pos(void)
	 * One cluster, cores are MPIDR.Aff0 0..3 -> pos = Aff0.
	 * ------------------------------------------------------------- */
func plat_my_core_pos
	mrs	x0, mpidr_el1
	b	plat_lg1314_calc_core_pos
endfunc plat_my_core_pos

	/* unsigned int plat_lg1314_calc_core_pos(u_register_t mpidr) */
func plat_lg1314_calc_core_pos
	and	x0, x0, #MPIDR_CPU_MASK
	ret
endfunc plat_lg1314_calc_core_pos

	/* ---------------------------------------------------------------
	 * unsigned int plat_is_my_cpu_primary(void)
	 * ------------------------------------------------------------- */
func plat_is_my_cpu_primary
	mrs	x0, mpidr_el1
	mov_imm	x1, (MPIDR_CLUSTER_MASK | MPIDR_CPU_MASK)
	and	x0, x0, x1
	cmp	x0, #LG1314_PRIMARY_CPU
	cset	w0, eq
	ret
endfunc plat_is_my_cpu_primary

	/* ---------------------------------------------------------------
	 * uintptr_t plat_get_my_entrypoint(void)
	 * Warm-boot: return the entrypoint published in the mailbox, or 0
	 * for a cold boot (primary).
	 * ------------------------------------------------------------- */
func plat_get_my_entrypoint
	mov_imm	x0, PLAT_LG1314_TRUSTED_MAILBOX_BASE
	/* 32-bit: mailbox is a 32-bit reg at a 4-byte (not 8-byte) aligned addr;
	 * a 64-bit ldr alignment-faults on the MMU-off (Device) cold path. */
	ldr	w0, [x0]
	ret
endfunc plat_get_my_entrypoint

	/* ---------------------------------------------------------------
	 * void plat_secondary_cold_boot_setup(void)
	 * In our model secondaries are released via PSCI; a core that cold-
	 * boots unexpectedly just parks.
	 * ------------------------------------------------------------- */
func plat_secondary_cold_boot_setup
cold_boot_wait:
	wfe
	b	cold_boot_wait
endfunc plat_secondary_cold_boot_setup

	/* ---------------------------------------------------------------
	 * Crash console (PL011). The prior boot loader already configured the UART.
	 * ------------------------------------------------------------- */
func plat_crash_console_init
	mov_imm	x0, PLAT_LG1314_UART_BASE
	mov_imm	x1, PLAT_LG1314_UART_CLK_IN_HZ
	mov_imm	x2, PLAT_LG1314_UART_BAUDRATE
	b	console_pl011_core_init
endfunc plat_crash_console_init

func plat_crash_console_putc
	mov_imm	x1, PLAT_LG1314_UART_BASE
	b	console_pl011_core_putc
endfunc plat_crash_console_putc

func plat_crash_console_flush
	mov_imm	x0, PLAT_LG1314_UART_BASE
	b	console_pl011_core_flush
endfunc plat_crash_console_flush

	/* ---------------------------------------------------------------
	 * void platform_mem_init(void)
	 * ------------------------------------------------------------- */
func platform_mem_init
	ret
endfunc platform_mem_init
