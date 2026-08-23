/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_STUB_DATA_H
#define __ARCH_STUB_DATA_H

#define STUB_SYNC_TLS (1 << 0)

/*
 * stub_init_data.arch_flags bit: disable the host's pointer
 * authentication keys in the freshly exec'd stub. Set by the boot-time
 * probe in os-Linux/pac.c only where the host NOPs PAC instructions
 * whose key is disabled; see stub_arch_init() in sysdep/stub.h.
 */
#define STUB_INIT_PAC_OFF (1UL << 0)

struct stub_data_arch {
	int sync;
	unsigned long tls;	/* TPIDR_EL0 value to restore in the stub */
};

#endif /* __ARCH_STUB_DATA_H */
