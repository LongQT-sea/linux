// SPDX-License-Identifier: GPL-2.0
/*
 * Host CPU feature discovery and /proc/cpuinfo display for the x86 backend.
 * Moved verbatim out of arch/um/kernel/um_arch.c.
 */
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>
#include <arch.h>

void arch_parse_cpu_flags(char *line)
{
	int i;

	for (i = 0; i < 32*NCAPINTS; i++) {
		if ((x86_cap_flags[i] != NULL) && strstr(line, x86_cap_flags[i]))
			set_cpu_cap(&boot_cpu_data, i);
	}
}

void arch_cpuinfo_show_extra(struct seq_file *m)
{
	int i;

	seq_printf(m, "fpu\t\t: %s\n",
		   str_yes_no(cpu_has(&boot_cpu_data, X86_FEATURE_FPU)));
	seq_printf(m, "flags\t\t:");
	for (i = 0; i < 32*NCAPINTS; i++)
		if (cpu_has(&boot_cpu_data, i) && (x86_cap_flags[i] != NULL))
			seq_printf(m, " %s", x86_cap_flags[i]);
	seq_printf(m, "\n");
}
