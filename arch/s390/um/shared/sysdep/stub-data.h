/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_STUB_DATA_H
#define __ARCH_STUB_DATA_H

/*
 * s390x needs no stub-carried arch state: TLS lives in acrs[0..1],
 * which are part of NT_PRSTATUS and of the SIGSYS mcontext — visible
 * to the kernel on both consumption routes (F-s1). The struct stays
 * (empty) so the shared stub-data.h include resolves, like x86-64's
 * pre-FSGSBASE shape but truly empty.
 */
struct stub_data_arch {
};

#endif /* __ARCH_STUB_DATA_H */
