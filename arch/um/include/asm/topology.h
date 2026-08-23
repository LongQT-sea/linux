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

#include <asm-generic/topology.h>

#endif /* __ASM_UM_TOPOLOGY_H */
