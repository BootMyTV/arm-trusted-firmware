/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <platform_def.h>

#include <plat/common/platform.h>

uintptr_t plat_get_ns_image_entrypoint(void)
{
	return (uintptr_t)PLAT_LG1314_NS_IMAGE_BASE;
}

unsigned int plat_get_syscnt_freq2(void)
{
	return LG1314_SYS_CNT_FREQ;
}
