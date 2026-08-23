/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __KERN_UTIL_H__
#define __KERN_UTIL_H__

#include <sysdep/ptrace.h>
#include <sysdep/faultinfo.h>

struct siginfo;

/*
 * The host-signal delivery chain must not be probed by kprobes: a probe
 * inside it would re-enter the chain on its own SIGTRAP and recurse until
 * the stack overflows. Host-side (USER_OBJS) code cannot use
 * NOKPROBE_SYMBOL(); this attribute places such functions in
 * .kprobes.text, which the kprobe core refuses to probe.
 */
#define __uml_nokprobe __attribute__((__section__(".kprobes.text")))

extern int uml_exitcode;

extern int kmalloc_ok;

extern unsigned long alloc_stack(int order, int atomic);
extern void free_stack(unsigned long stack, int order);

struct pt_regs;
extern void do_signal(struct pt_regs *regs);
extern void interrupt_end(void);
extern void relay_signal(int sig, struct siginfo *si, struct uml_pt_regs *regs,
			 void *mc);

extern unsigned long segv(struct faultinfo fi, unsigned long ip,
			  int is_user, struct uml_pt_regs *regs,
			  void *mc);
extern int handle_page_fault(unsigned long address, unsigned long ip,
			     int is_write, int is_user, int *code_out);

extern unsigned int do_IRQ(int irq, struct uml_pt_regs *regs);
extern void initial_thread_cb(void (*proc)(void *), void *arg);

extern void timer_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs);

extern void uml_pm_wake(void);

extern int start_uml(void);

extern void uml_cleanup(void);
extern void do_uml_exitcalls(void);

/*
 * Are we disallowed to sleep? Used to choose between GFP_KERNEL and
 * GFP_ATOMIC.
 */
extern int __uml_cant_sleep(void);
extern int get_current_pid(void);
extern int copy_from_user_proc(void *to, void *from, int size);
extern char *uml_strdup(const char *string);
int uml_need_resched(void);

extern unsigned long to_irq_stack(unsigned long *mask_out);
extern unsigned long from_irq_stack(int nested);

extern int singlestepping(void);

extern void segv_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs,
			 void *mc);
extern void winch(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs,
		  void *mc);
extern void fatal_sigsegv(void) __attribute__ ((noreturn));

void um_idle_sleep(void);

void kasan_map_memory(void *start, size_t len);

#endif
