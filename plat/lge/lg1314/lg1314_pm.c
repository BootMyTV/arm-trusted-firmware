/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <platform_def.h>

#include <arch_helpers.h>
#include <common/debug.h>
#include <drivers/arm/gicv2.h>
#include <lib/mmio.h>
#include <lib/psci/psci.h>
#include <plat/common/platform.h>

/* Secondary-CPU warm-boot entrypoint, published to the SMP pen at CPU_ON. */
static uintptr_t lg1314_sec_entrypoint;

/*
 * WFI pen the prior boot loader parks cpu1-3 in: a `wfi; ldr; cbz; br` loop at
 * a fixed address they spin in until released. pwr_domain_on patches the 3
 * instructions after the wfi (+0x04..+0x0c) to branch to lg1314_sec_entrypoint,
 * then wakes the core.
 */
#define LG1314_SMP_PEN		UL(0xfff01e10)
#define GICD_SGIR_OFF		U(0xf00)

static int lg1314_validate_power_state(unsigned int power_state,
				       psci_power_state_t *req_state)
{
	/* Only CPU-level standby/off for now. */
	return PSCI_E_INVALID_PARAMS;
}

#define CPUPWR_TIMEOUT	0xfffffU

/*
 * CPUPWR rail power-up for a secondary core (cpu == 1/2/3). Per-cpu lanes:
 * cpu1 uses the high byte/half of each field, cpu2 the middle, cpu3 the low.
 * Returns 0 on success. Only used on the truly-off cold-start fallback.
 */
static int lg1314_cpupwr_up(unsigned int cpu)
{
	uintptr_t b = PLAT_LG1314_CPUPWR_BASE;
	uint32_t cpubit = 1U << cpu;		/* cpu1->2, cpu2->4, cpu3->8 */
	uint32_t lo = cpubit & 0xffU;
	uint32_t v;
	unsigned int step, i;

	/* Pre: clear this cpu's request bit in RAMP_REQ[12:15]. */
	v = mmio_read_32(b + CPUPWR_RAMP_REQ);
	v = (v & 0xffff0000U) | (v & 0xfffU) |
	    (((v >> 12) & 0xfU & ~lo) << 12);
	mmio_write_32(b + CPUPWR_RAMP_REQ, v);

	/* 8-step power-switch ramp (RAMP), poll RAMP_ACK per step. */
	for (step = 0; step < 8U; step++) {
		v = mmio_read_32(b + CPUPWR_RAMP);
		if (cpu == 1U)
			v = (v & 0xff000000U) | (v & 0xffffU) |
			    ((((1U << step) & 0xffU) | ((v >> 16) & 0xffU)) << 16);
		else if (cpu == 2U)
			v = (v & 0xffff0000U) | (v & 0xffU) |
			    ((((1U << step) & 0xffU) | ((v >> 8) & 0xffU)) << 8);
		else
			v = (v & 0xffffff00U) | ((v | (1U << step)) & 0xffU);
		mmio_write_32(b + CPUPWR_RAMP, v);

		for (i = CPUPWR_TIMEOUT; ; i--) {
			v = mmio_read_32(b + CPUPWR_RAMP_ACK);
			if (cpu == 1U)
				v >>= 16;
			else if (cpu == 2U)
				v >>= 8;
			if ((v & 0xffU & (1U << step)) != 0U)
				break;
			if (i == 0U) {
				ERROR("BL31: cpu%u CPUPWR ramp timeout\n", cpu);
				return -1;
			}
		}
	}

	/* 16-step clamp/isolation release. */
	for (step = 0; step < 16U; step++) {
		if (cpu == 1U) {
			v = mmio_read_32(b + CPUPWR_CLAMP0);
			v = (v & 0xffff0000U) |
			    (v & ~(1U << step) & 0xffffU);
			mmio_write_32(b + CPUPWR_CLAMP0, v);
		} else if (cpu == 2U) {
			v = mmio_read_32(b + CPUPWR_CLAMP1);
			v = (v & 0xffffU) |
			    (((v >> 16) & ~(1U << step)) << 16);
			mmio_write_32(b + CPUPWR_CLAMP1, v);
		} else {
			v = mmio_read_32(b + CPUPWR_CLAMP1);
			v = (v & 0xffff0000U) |
			    (v & ~(1U << step) & 0xffffU);
			mmio_write_32(b + CPUPWR_CLAMP1, v);
		}
	}

	/* Core enable (CORE_ENABLE bit cpubit) + set RAMP_REQ[12:15] bit. */
	v = mmio_read_32(b + CPUPWR_CORE_ENABLE);
	mmio_write_32(b + CPUPWR_CORE_ENABLE, v | cpubit);

	v = mmio_read_32(b + CPUPWR_RAMP_REQ);
	v = (v & 0xffff0000U) | (v & 0xfffU) |
	    (((lo & 0xfU) | ((v >> 12) & 0xfU)) << 12);
	mmio_write_32(b + CPUPWR_RAMP_REQ, v);

	return 0;
}

/*
 * Power a secondary core on:
 *   - If the core is genuinely off (SMPEN clear), ramp its CPUPWR rails and
 *     wait for SMPEN. Normally the prior boot loader leaves the core powered
 *     and parked in the WFI pen, so this fast-paths straight to the release.
 *   - Release the core from the pen to bl31_warm_entrypoint (see below).
 */
static int lg1314_pwr_domain_on(u_register_t mpidr)
{
	int pos = plat_core_pos_by_mpidr(mpidr);
	unsigned int cpu, i;
	uint32_t cpubit, ep;

	if (pos <= 0)
		return PSCI_E_INVALID_PARAMS;	/* cpu0 is always on */
	cpu = (unsigned int)pos;
	cpubit = 1U << cpu;

	if ((mmio_read_32(PLAT_LG1314_CPUPWR_BASE + CPUPWR_STATUS) &
	     0xfU & cpubit) == 0U) {
		if (lg1314_cpupwr_up(cpu) != 0)
			return PSCI_E_INTERN_FAIL;

		for (i = CPUPWR_TIMEOUT; ; i--) {
			if ((mmio_read_32(PLAT_LG1314_CPUPWR_BASE +
					  CPUPWR_STATUS) & 0xfU & cpubit) != 0U)
				break;
			if (i == 0U) {
				ERROR("BL31: cpu%u smpen not asserted\n", cpu);
				return PSCI_E_INTERN_FAIL;
			}
		}
	}

	/*
	 * Release the secondary. The prior boot loader leaves cpu1-3 in a WFI
	 * pen at LG1314_SMP_PEN:
	 *
	 *     wfi ; ldr w,[mbox] ; cbz w,. ; br x
	 *
	 * We do not drive the pen's own mailbox (its address varies), and the
	 * core runs MMU-off. Instead we patch the three instructions after the
	 * `wfi` so that on wake the core branches straight to the warm
	 * entrypoint. The entry is built with movz/movk
	 * (no memory access): the core is still MMU-off (Device), where a
	 * non-8-aligned 64-bit ldr-literal would alignment-fault. Its I-cache
	 * is off (the pen disabled it), so a dsb is enough for the new
	 * instructions to be fetched.
	 */
	ep = (uint32_t)lg1314_sec_entrypoint;
	mmio_write_32(LG1314_SMP_PEN + 0x04U,
		      0x52800004U | ((ep & 0xffffU) << 5));	/* movz w4, #lo */
	mmio_write_32(LG1314_SMP_PEN + 0x08U,
		      0x72a00004U | (((ep >> 16) & 0xffffU) << 5));	/* movk w4, #hi, lsl 16 */
	mmio_write_32(LG1314_SMP_PEN + 0x0cU, 0xd61f0080U);	/* br x4 */
	dsbsy();

	/*
	 * Wake the WFI with a group0 SGI (ID 0) targeting the core's GIC-400
	 * interface bit (1<<cpu), written straight to GICD_SGIR. The IRQ is
	 * masked in the pen (DAIF.I set) so it wakes without being taken.
	 * gicv2_raise_sgi() can't be used: it indexes target_masks[], which is
	 * populated only once a core runs its own pcpu init, so for a not-yet-
	 * online core the mask is 0 and the SGI reaches nobody.
	 */
	mmio_write_32(LG1314_GICD_BASE + GICD_SGIR_OFF, (cpubit & 0xffU) << 16);

	return PSCI_E_SUCCESS;
}

static void lg1314_pwr_domain_on_finish(const psci_power_state_t *target_state)
{
	/* Bring up this core's GIC CPU interface. */
	gicv2_pcpu_distif_init();
	gicv2_cpuif_enable();
}

static void lg1314_pwr_domain_off(const psci_power_state_t *target_state)
{
	gicv2_cpuif_disable();
}

static void __dead2 lg1314_pwr_down_wfi(const psci_power_state_t *target_state)
{
	dsbsy();
	for (;;)
		wfi();
}

static void __dead2 lg1314_system_off(void)
{
	ERROR("BL31: PSCI_SYSTEM_OFF not implemented\n");	/* TODO */
	for (;;)
		wfi();
}

static void __dead2 lg1314_system_reset(void)
{
	ERROR("BL31: PSCI_SYSTEM_RESET not implemented\n");	/* TODO */
	for (;;)
		wfi();
}

static const plat_psci_ops_t lg1314_psci_ops = {
	.pwr_domain_on		= lg1314_pwr_domain_on,
	.pwr_domain_on_finish	= lg1314_pwr_domain_on_finish,
	.pwr_domain_off		= lg1314_pwr_domain_off,
	.pwr_domain_pwr_down	= lg1314_pwr_down_wfi,
	.system_off		= lg1314_system_off,
	.system_reset		= lg1314_system_reset,
	.validate_power_state	= lg1314_validate_power_state,
};

int plat_setup_psci_ops(uintptr_t sec_entrypoint,
			const plat_psci_ops_t **psci_ops)
{
	/* Saved for pwr_domain_on, which patches it into the per-core pen. */
	lg1314_sec_entrypoint = sec_entrypoint;

	*psci_ops = &lg1314_psci_ops;
	return 0;
}
