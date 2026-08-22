// SPDX-License-Identifier: GPL-2.0
/*
 * Host CPU feature discovery and /proc/cpuinfo display — s390x
 * backend. The host's "facilities:" list is captured minus the
 * facilities whose CPU state the port cannot round-trip across stub
 * traps and signal frames (see ELF_HWCAP in asm/elf.h): the vector
 * family (129, 134, 135, 148, 152, 192), guarded storage (133) and
 * NNPA (165), so cpuinfo does not contradict the masked AT_HWCAP.
 */
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <arch.h>

static char s390_facility_line[512];

void arch_parse_cpu_flags(char *line)
{
	static const unsigned long hidden[] = {
		129, 133, 134, 135, 148, 152, 165, 192,
	};
	char *p, *out;
	size_t left;

	if (strncmp(line, "facilities", 10))
		return;
	p = strchr(line, ':');
	if (!p)
		return;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;
	out = s390_facility_line;
	left = sizeof(s390_facility_line);
	while (*p && left > 1) {
		unsigned long f;
		char *end;
		bool keep = true;
		size_t n;
		int i, w;

		f = simple_strtoul(p, &end, 10);
		n = end - p;
		if (!n)
			break;
		for (i = 0; i < ARRAY_SIZE(hidden); i++)
			if (hidden[i] == f)
				keep = false;
		if (keep) {
			w = scnprintf(out, left, "%.*s ", (int)n, p);
			if (w <= 0)
				break;
			out += w;
			left -= w;
		}
		p = end;
		while (*p == ' ')
			p++;
	}
	if (out > s390_facility_line)
		out[-1] = '\0';
	else
		s390_facility_line[0] = '\0';
}

void arch_cpuinfo_show_extra(struct seq_file *m)
{
	seq_printf(m, "fpu\t\t: yes\n");
	seq_puts(m, "facilities    :");
	if (s390_facility_line[0])
		seq_printf(m, " %s", s390_facility_line);
	else
		seq_putc(m, '\n');
}
