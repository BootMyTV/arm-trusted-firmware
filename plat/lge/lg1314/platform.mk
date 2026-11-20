# SPDX-License-Identifier: BSD-3-Clause
#
# LG1314 (M19A1) BL31-only port, entered RESET_TO_BL31 by the prior boot
# loader, which does all SoC init. BL31 provides PSCI + a null secure world
# and hands off to a preloaded kernel (BL33).

include lib/xlat_tables_v2/xlat_tables.mk
include drivers/arm/gic/v2/gicv2.mk

PLAT_INCLUDES		:=	-Iplat/lge/lg1314/include

PLAT_BL_COMMON_SOURCES	:=	drivers/arm/pl011/aarch64/pl011_console.S	\
				plat/lge/lg1314/aarch64/plat_helpers.S		\
				plat/lge/lg1314/lg1314_common.c			\
				${XLAT_TABLES_LIB_SRCS}

BL31_SOURCES		+=	${GICV2_SOURCES}				\
				plat/common/plat_gicv2.c			\
				drivers/arm/tzc/tzc400.c			\
				plat/common/plat_psci_common.c			\
				lib/cpus/aarch64/cortex_a53.S			\
				plat/lge/lg1314/lg1314_bl31_setup.c		\
				plat/lge/lg1314/lg1314_pm.c			\
				plat/lge/lg1314/lg1314_topology.c

# BL31 is the reset/entry stage; no BL1/BL2. The prior boot loader preloads
# BL33 (the kernel) and jumps to bl31_entrypoint. Secondaries come up via PSCI.
RESET_TO_BL31			:=	1
PROGRAMMABLE_RESET_ADDRESS	:=	0
COLD_BOOT_SINGLE_CPU		:=	1

# A53: use the generic timer at the SoC counter frequency; GICv2.
ARM_ARCH_MAJOR			:=	8
HW_ASSISTED_COHERENCY		:=	0
USE_COHERENT_MEM		:=	1

# Default BL33 (kernel) load address: where the boot loader places the kernel
# Image. Must match the loader's configuration; override on the make line if
# the loader places it elsewhere. 2 MiB-aligned, as the arm64 boot protocol
# requires for an Image with text_offset 0 (Documentation/arch/arm64/booting.rst).
PRELOADED_BL33_BASE		?=	0x00200000
$(eval $(call add_define_val,PLAT_LG1314_NS_IMAGE_BASE,$(PRELOADED_BL33_BASE)))
