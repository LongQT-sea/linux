/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_ARM64_RQSPINLOCK_H
#define __UM_ARM64_RQSPINLOCK_H

/*
 * arm64's resilient spinlocks wait with WFE only where the arch timer's event
 * stream is known to end the wait, so that the timeout is checked. Under UML
 * that is the host's business, and unknown: spin, checking the timeout.
 */
#define arch_timer_evtstrm_available()	false

#include "../../include/asm/rqspinlock.h"

#endif
