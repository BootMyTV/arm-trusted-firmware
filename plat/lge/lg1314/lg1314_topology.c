/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <platform_def.h>

#include <arch.h>
#include <arch_helpers.h>
#include <plat/common/platform.h>

/* 1 cluster with 4 cores. */
static const unsigned char lg1314_power_domain_tree_desc[] = {
	PLATFORM_CLUSTER_COUNT,
	PLATFORM_CLUSTER0_CORE_COUNT,
};

const unsigned char *plat_get_power_domain_tree_desc(void)
{
	return lg1314_power_domain_tree_desc;
}

/*
 * MPIDR -> linear core index. One cluster (Aff1 == 0), cores are Aff0 0..3.
 */
int plat_core_pos_by_mpidr(u_register_t mpidr)
{
	unsigned int cluster_id, cpu_id;

	mpidr &= MPIDR_AFFINITY_MASK;
	if ((mpidr & ~(MPIDR_CLUSTER_MASK | MPIDR_CPU_MASK)) != 0U)
		return -1;

	cluster_id = (unsigned int)((mpidr >> MPIDR_AFF1_SHIFT) &
				    MPIDR_AFFLVL_MASK);
	cpu_id = (unsigned int)((mpidr >> MPIDR_AFF0_SHIFT) &
				MPIDR_AFFLVL_MASK);

	if (cluster_id >= PLATFORM_CLUSTER_COUNT)
		return -1;
	if (cpu_id >= PLATFORM_CLUSTER0_CORE_COUNT)
		return -1;

	return (int)cpu_id;
}
