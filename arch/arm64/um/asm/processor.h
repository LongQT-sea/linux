/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_ARM64_PROCESSOR_H
#define __UM_ARM64_PROCESSOR_H
#include <linux/time-internal.h>

/* include faultinfo structure */
#include <sysdep/faultinfo.h>

#define KSTK_EIP(tsk) KSTK_REG(tsk, HOST_PC)
#define KSTK_ESP(tsk) KSTK_REG(tsk, HOST_SP)
#define KSTK_EBP(tsk) KSTK_REG(tsk, HOST_X29)

#define ARCH_IS_STACKGROW(address) \
       (address + 65536 + 32 * sizeof(unsigned long) >= UPT_SP(&current->thread.regs.regs))

#include <asm/user.h>

/* YIELD is a good thing to insert into busy-wait loops. */
static __always_inline void native_pause(void)
{
	__asm__ __volatile__("yield" : : : "memory");
}

static __always_inline void cpu_relax(void)
{
	if (time_travel_mode == TT_MODE_INFCPU ||
	    time_travel_mode == TT_MODE_EXTERNAL)
		time_travel_ndelay(1);
	else
		native_pause();
}

#define task_pt_regs(t) (&(t)->thread.regs)

#include <asm/processor-generic.h>

#endif
