// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <init.h>
#include <as-layout.h>
#include <mm_id.h>
#include <os.h>
#include <ptrace_user.h>
#include <registers.h>
#include <skas.h>
#include <sysdep/ptrace.h>
#include <sysdep/stub.h>
#include "../internal.h"

extern char __syscall_stub_start[];

void syscall_stub_dump_error(struct stub_thread *st)
{
	struct stub_data *proc_data = (void *)st->stack;
	struct stub_syscall *sc;

	if (proc_data->syscall_data_len < 0 ||
	    proc_data->syscall_data_len >= ARRAY_SIZE(proc_data->syscall_data))
		panic("Syscall data was corrupted by stub (len is: %d, expected maximum: %d)!",
			proc_data->syscall_data_len,
			st->syscall_data_len);

	sc = &proc_data->syscall_data[proc_data->syscall_data_len];

	printk(UM_KERN_ERR "%s : length = %d, last offset = %d",
		__func__, st->syscall_data_len,
		proc_data->syscall_data_len);
	printk(UM_KERN_ERR "%s : stub syscall type %d failed, return value = 0x%lx\n",
		__func__, sc->syscall, proc_data->err);

	print_hex_dump(UM_KERN_ERR, "    syscall data: ", 0,
		       16, 4, sc, sizeof(*sc), 0);

	if (using_seccomp) {
		printk(UM_KERN_ERR "%s: FD map num: %d", __func__,
		       st->syscall_fd_num);
		print_hex_dump(UM_KERN_ERR,
				"    FD map: ", 0, 16,
				sizeof(st->syscall_fd_map[0]),
				st->syscall_fd_map,
				sizeof(st->syscall_fd_map), 0);
	}
}

static unsigned long syscall_regs[MAX_REG_NR];

static int __init init_syscall_regs(void)
{
	get_safe_registers(syscall_regs, NULL);

	syscall_regs[REGS_IP_INDEX] = STUB_CODE +
		((unsigned long) stub_syscall_handler -
		 (unsigned long) __syscall_stub_start);
	syscall_regs[REGS_SP_INDEX] = STUB_DATA +
		offsetof(struct stub_data, sigstack) +
		sizeof(((struct stub_data *) 0)->sigstack) -
		sizeof(void *);

	return 0;
}

__initcall(init_syscall_regs);

static inline long do_syscall_stub(struct stub_thread *st)
{
	struct stub_data *proc_data = (void *)st->stack;
	int n, i;
	int err, pid = st->mm->pid;

	/* Inform process how much we have filled in. */
	proc_data->syscall_data_len = st->syscall_data_len;

	if (using_seccomp) {
		proc_data->restart_wait = 1;
		if (wait_stub_done_seccomp(st, 0, 1, 1)) {
			/* The stub is gone, report as an error */
			st->syscall_data_len = -EIO;
			st->syscall_fd_num = 0;
			return st->syscall_data_len;
		}
	} else {
		n = ptrace_setregs(pid, syscall_regs);
		if (n < 0) {
			printk(UM_KERN_ERR "Registers -\n");
			for (i = 0; i < MAX_REG_NR; i++)
				printk(UM_KERN_ERR "\t%d\t0x%lx\n", i, syscall_regs[i]);
			panic("%s : PTRACE_SETREGS failed, errno = %d\n",
			      __func__, -n);
		}

		err = ptrace(PTRACE_CONT, pid, 0, 0);
		if (err)
			panic("Failed to continue stub, pid = %d, errno = %d\n",
			      pid, errno);

		wait_stub_done(pid);
	}

	/*
	 * proc_data->err will be negative if there was an (unexpected) error.
	 * In that case, syscall_data_len points to the last executed syscall,
	 * otherwise it will be zero (but we do not need to rely on that).
	 */
	if (proc_data->err < 0) {
		syscall_stub_dump_error(st);

		/* Store error code in case someone tries to add more syscalls */
		st->syscall_data_len = proc_data->err;
	} else {
		st->syscall_data_len = 0;
	}

	if (using_seccomp)
		st->syscall_fd_num = 0;

	return st->syscall_data_len;
}

int syscall_stub_flush(struct stub_thread *st)
{
	int res;

	if (st->syscall_data_len == 0)
		return 0;

	/* If an error happened already, report it and reset the state. */
	if (st->syscall_data_len < 0) {
		res = st->syscall_data_len;
		st->syscall_data_len = 0;
		return res;
	}

	res = do_syscall_stub(st);
	st->syscall_data_len = 0;

	return res;
}

struct stub_syscall *syscall_stub_alloc(struct stub_thread *st)
{
	struct stub_syscall *sc;
	struct stub_data *proc_data = (struct stub_data *)st->stack;

	if (st->syscall_data_len > 0 &&
	    st->syscall_data_len == ARRAY_SIZE(proc_data->syscall_data))
		do_syscall_stub(st);

	if (st->syscall_data_len < 0) {
		/* Return dummy to retain error state. */
		sc = &proc_data->syscall_data[0];
	} else {
		sc = &proc_data->syscall_data[st->syscall_data_len];
		st->syscall_data_len += 1;
	}
	memset(sc, 0, sizeof(*sc));

	return sc;
}

static struct stub_syscall *syscall_stub_get_previous(struct stub_thread *st,
						      int syscall_type,
						      unsigned long virt)
{
	if (st->syscall_data_len > 0) {
		struct stub_data *proc_data = (void *)st->stack;
		struct stub_syscall *sc;

		sc = &proc_data->syscall_data[st->syscall_data_len - 1];

		if (sc->syscall == syscall_type &&
		    sc->mem.addr + sc->mem.length == virt)
			return sc;
	}

	return NULL;
}

static int get_stub_fd(struct stub_thread *st, int fd)
{
	int i;

	/* Find an FD slot (or flush and use first) */
	if (!using_seccomp)
		return fd;

	/* Already crashed, value does not matter */
	if (st->syscall_data_len < 0)
		return 0;

	/* Find existing FD in map if we can allocate another syscall */
	if (st->syscall_data_len <
	    ARRAY_SIZE(((struct stub_data *)NULL)->syscall_data)) {
		for (i = 0; i < st->syscall_fd_num; i++) {
			if (st->syscall_fd_map[i] == fd)
				return i;
		}

		if (st->syscall_fd_num < STUB_MAX_FDS) {
			i = st->syscall_fd_num;
			st->syscall_fd_map[i] = fd;

			st->syscall_fd_num++;

			return i;
		}
	}

	/* FD map full or no syscall space available, continue after flush */
	do_syscall_stub(st);
	st->syscall_fd_map[0] = fd;
	st->syscall_fd_num = 1;

	return 0;
}

int map(struct stub_thread *st, unsigned long virt, unsigned long len, int prot,
	int phys_fd, unsigned long long offset)
{
	struct stub_syscall *sc;

	/* Compress with previous syscall if that is possible */
	sc = syscall_stub_get_previous(st, STUB_SYSCALL_MMAP, virt);
	if (sc && sc->mem.prot == prot &&
	    sc->mem.offset == MMAP_OFFSET(offset - sc->mem.length)) {
		int prev_fd = sc->mem.fd;

		if (using_seccomp)
			prev_fd = st->syscall_fd_map[sc->mem.fd];

		if (phys_fd == prev_fd) {
			sc->mem.length += len;
			return 0;
		}
	}

	phys_fd = get_stub_fd(st, phys_fd);

	sc = syscall_stub_alloc(st);
	sc->syscall = STUB_SYSCALL_MMAP;
	sc->mem.addr = virt;
	sc->mem.length = len;
	sc->mem.prot = prot;
	sc->mem.fd = phys_fd;
	sc->mem.offset = MMAP_OFFSET(offset);

	return 0;
}

int unmap(struct stub_thread *st, unsigned long addr, unsigned long len)
{
	struct stub_syscall *sc;

	/* Compress with previous syscall if that is possible */
	sc = syscall_stub_get_previous(st, STUB_SYSCALL_MUNMAP, addr);
	if (sc) {
		sc->mem.length += len;
		return 0;
	}

	sc = syscall_stub_alloc(st);
	sc->syscall = STUB_SYSCALL_MUNMAP;
	sc->mem.addr = addr;
	sc->mem.length = len;

	return 0;
}

#ifdef CONFIG_UML_MT_STUB
/* Queue the creation of a thread that uses the data area in @slot */
void stub_thread_clone(struct stub_thread *st, int slot)
{
	struct stub_syscall *sc;

	sc = syscall_stub_alloc(st);
	sc->syscall = STUB_SYSCALL_CLONE;
	sc->mem.addr = STUB_SLOT_DATA(slot);
	sc->mem.length = STUB_SLOT_SIZE;
}
#endif
