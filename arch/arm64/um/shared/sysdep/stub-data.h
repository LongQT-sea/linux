/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_STUB_DATA_H
#define __ARCH_STUB_DATA_H

#define STUB_SYNC_TLS (1 << 0)

struct stub_data_arch {
	int sync;
	unsigned long tls;	/* TPIDR_EL0 value to restore in the stub */
};

#endif /* __ARCH_STUB_DATA_H */
