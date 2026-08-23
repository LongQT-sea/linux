/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_UM_TOPOLOGY_H
#define __ASM_UM_TOPOLOGY_H

/*
 * UML presents a single package/die/cluster/book/drawer to the guest.
 * Report 0 (a real, parseable id) instead of the -1 defaults from
 * include/linux/topology.h: userspace topology parsers (e.g. scx_utils,
 * used by every Rust sched_ext scheduler) reject "-1" from
 * /sys/devices/system/cpu/cpu0/topology/ files.
 */
#define topology_physical_package_id(cpu)	((void)(cpu), 0)
#define topology_die_id(cpu)			((void)(cpu), 0)
#define topology_cluster_id(cpu)		((void)(cpu), 0)
#define topology_book_id(cpu)			((void)(cpu), 0)
#define topology_drawer_id(cpu)			((void)(cpu), 0)

#include <linux/cpumask.h>

/*
 * SCHED_CLUSTER's sched-domain code calls this; other arches provide it
 * from their topology/smp code. All UML CPUs form a single cluster.
 */
static inline const struct cpumask *cpu_clustergroup_mask(int cpu)
{
	return cpu_possible_mask;
}

/* both id and cpumask must be arch-defined for the sysfs attribute to exist */
#define topology_cluster_cpumask(cpu)		cpu_clustergroup_mask(cpu)

#include <asm-generic/topology.h>

#endif /* __ASM_UM_TOPOLOGY_H */
