// SPDX-License-Identifier: GPL-2.0
/*
 * Host CPU feature discovery and /proc/cpuinfo display for the
 * arm64 backend: capture the host's "Features" line verbatim (the
 * capability array in cpuinfo_um stays unused until a consumer
 * needs it).
 */
#include <linux/seq_file.h>
#include <linux/string.h>
#include <arch.h>

static char arm64_features[256];

void arch_parse_cpu_flags(char *line)
{
	char *p;

	if (strncmp(line, "Features", 8))
		return;
	p = strchr(line, ':');
	if (p) {
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		strscpy(arm64_features, p, sizeof(arm64_features));
	}
}

void arch_cpuinfo_show_extra(struct seq_file *m)
{
	seq_printf(m, "fpu\t\t: yes\n");
	seq_printf(m, "Features\t: %s", arm64_features);
	if (arm64_features[0] && !strchr(arm64_features, '\n'))
		seq_putc(m, '\n');
}
