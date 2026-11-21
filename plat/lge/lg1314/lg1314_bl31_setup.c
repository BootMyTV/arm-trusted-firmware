/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <assert.h>

#include <platform_def.h>

#include <arch_helpers.h>
#include <common/bl_common.h>
#include <common/debug.h>
#include <drivers/arm/gicv2.h>
#include <drivers/arm/pl011.h>
#include <drivers/arm/tzc400.h>
#include <drivers/console.h>
#include <drivers/generic_delay_timer.h>
#include <lib/mmio.h>
#include <lib/xlat_tables/xlat_tables_v2.h>
#include <plat/common/platform.h>

static entry_point_info_t bl33_image_ep_info;
static console_t lg1314_console;

/* GICv2 (GIC-400) driver data. */
static const interrupt_prop_t lg1314_gicv2_props[] = {
};

static const gicv2_driver_data_t lg1314_gic_data = {
	.gicd_base = LG1314_GICD_BASE,
	.gicc_base = LG1314_GICC_BASE,
	.interrupt_props = lg1314_gicv2_props,
	.interrupt_props_num = ARRAY_SIZE(lg1314_gicv2_props),
};

/*
 * Return the entrypoint for the next image. We only ever boot a non-secure
 * BL33 (the kernel); there is no BL32.
 */
entry_point_info_t *bl31_plat_get_next_image_ep_info(uint32_t type)
{
	assert(sec_state_is_valid(type));
	if (type == NON_SECURE)
		return &bl33_image_ep_info;
	return NULL;
}

void bl31_early_platform_setup2(u_register_t arg0, u_register_t arg1,
				u_register_t arg2, u_register_t arg3)
{
	/* core_init rejects a zero clk/baud, so pass the real divisor inputs. */
	(void)console_pl011_register(PLAT_LG1314_UART_BASE,
				     PLAT_LG1314_UART_CLK,
				     PLAT_LG1314_UART_BAUDRATE,
				     &lg1314_console);
	console_set_scope(&lg1314_console, CONSOLE_FLAG_BOOT |
			  CONSOLE_FLAG_RUNTIME | CONSOLE_FLAG_CRASH);

	NOTICE("BL31: LG1314 (M19A1) RESET_TO_BL31\n");

	/* Hand off to the preloaded kernel at NS-EL2, X0 = DTB. */
	SET_PARAM_HEAD(&bl33_image_ep_info, PARAM_EP, VERSION_1, 0);
	SET_SECURITY_STATE(bl33_image_ep_info.h.attr, NON_SECURE);
	bl33_image_ep_info.pc = plat_get_ns_image_entrypoint();
	bl33_image_ep_info.spsr = SPSR_64(MODE_EL2, MODE_SP_ELX,
					  DISABLE_ALL_EXCEPTIONS);
	/* DTB phys may be passed by the boot loader in x0; else use the default. */
	bl33_image_ep_info.args.arg0 = (arg0 != 0U) ? arg0 :
					(u_register_t)PLAT_LG1314_DTB_BASE;

	(void)arg1; (void)arg2; (void)arg3;
}

void bl31_plat_arch_setup(void)
{
	/* Whole BL31 window RW, then refine .text/.rodata (MT_RW is XN, so the
	 * coarse region alone leaves our own code non-executable under the MMU). */
	mmap_add_region(BL31_BASE, BL31_BASE,
			BL31_LIMIT - BL31_BASE,
			MT_MEMORY | MT_RW | MT_SECURE);
	mmap_add_region(BL_CODE_BASE, BL_CODE_BASE,
			BL_CODE_END - BL_CODE_BASE,
			MT_CODE | MT_SECURE);
	mmap_add_region(BL_RO_DATA_BASE, BL_RO_DATA_BASE,
			BL_RO_DATA_END - BL_RO_DATA_BASE,
			MT_RO_DATA | MT_SECURE);
#if USE_COHERENT_MEM
	mmap_add_region(BL_COHERENT_RAM_BASE, BL_COHERENT_RAM_BASE,
			BL_COHERENT_RAM_END - BL_COHERENT_RAM_BASE,
			MT_DEVICE | MT_RW | MT_SECURE);
#endif
	mmap_add_region(LG1314_SEC_BASE, LG1314_SEC_BASE,
			LG1314_SEC_SIZE,
			MT_MEMORY | MT_RW | MT_SECURE);
	mmap_add_region(LG1314_DRAM_BASE, LG1314_DRAM_BASE,
			LG1314_DRAM_SIZE,
			MT_MEMORY | MT_RW | MT_NS);
	mmap_add_region(LG1314_DEVICE_BASE, LG1314_DEVICE_BASE,
			LG1314_DEVICE_SIZE,
			MT_DEVICE | MT_RW | MT_SECURE);

	init_xlat_tables();
	enable_mmu_el3(0);
}

/*
 * Open DRAM to the non-secure world but keep BL31's own region secure. Per
 * TZC-400 instance: region 0 (background) = secure RW + every NS master RW, so
 * REE owns all DRAM; region 1 = BL31's region, secure-only (NS denied), which
 * overrides the background for that range and protects EL3 from a buggy or
 * compromised non-secure Linux. Violations abort (TZC_ACTION_ERR). We run no
 * TEE; the GIC init routes all interrupts to NS (Group 1).
 */
static void lg1314_tzc_open_one(uintptr_t base)
{
	tzc400_init(base);
	tzc400_configure_region0(TZC_REGION_S_RDWR, 0xffffffffU);
	tzc400_configure_region(TZC_400_REGION_ATTR_FILTER_BIT_ALL, 1,
				LG1314_SEC_BASE,
				LG1314_SEC_BASE + LG1314_SEC_SIZE - 1ULL,
				TZC_REGION_S_RDWR, 0U);
	tzc400_set_action(TZC_ACTION_ERR);
	tzc400_enable_filters();
}

static void lg1314_tzc_open_to_ns(void)
{
	lg1314_tzc_open_one(LG1314_TZASC0_BASE);
	lg1314_tzc_open_one(LG1314_TZASC1_BASE);
}

void bl31_platform_setup(void)
{
	generic_delay_timer_init();

	gicv2_driver_init(&lg1314_gic_data);
	gicv2_distif_init();
	gicv2_pcpu_distif_init();
	gicv2_cpuif_enable();

	lg1314_tzc_open_to_ns();
}
