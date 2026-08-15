/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>

OUTPUT_FORMAT(ELF_FORMAT)
OUTPUT_ARCH(ELF_ARCH)
ENTRY(_start)
jiffies = jiffies_64;

VERSION {
  {
    local: *;
  };
}

SECTIONS
{
  /* This must contain the right address - not quite the default ELF one.*/
  PROVIDE (__executable_start = START);
  /* Static binaries stick stuff here, like the sigreturn trampoline,
   * invisibly to objdump.  So, just make __binary_start equal to the very
   * beginning of the executable, and if there are unmapped pages after this,
   * they are forever unusable.
   */
  __binary_start = START;

  . = START + SIZEOF_HEADERS;
  . = ALIGN(PAGE_SIZE);

  _text = .;
  INIT_TEXT_SECTION(0)

  .text      :
  {
    _stext = .;
    TEXT_TEXT
    SCHED_TEXT
    LOCK_TEXT
    IRQENTRY_TEXT
    SOFTIRQENTRY_TEXT
    *(.fixup)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    *(.gnu.linkonce.t*)
  }

  . = ALIGN(PAGE_SIZE);
  .syscall_stub : {
	__syscall_stub_start = .;
	*(.__syscall_stub*)
	__syscall_stub_end = .;
  }

  /*
   * These are needed even in a static link, even if they wind up being empty.
   * Newer glibc needs these __rel{,a}_iplt_{start,end} symbols.
   */
  .rel.plt : {
	*(.rel.plt)
	PROVIDE_HIDDEN(__rel_iplt_start = .);
	*(.rel.iplt)
	PROVIDE_HIDDEN(__rel_iplt_end = .);
  }
  .rela.plt : {
	*(.rela.plt)
	PROVIDE_HIDDEN(__rela_iplt_start = .);
	*(.rela.iplt)
	PROVIDE_HIDDEN(__rela_iplt_end = .);
  }

  #include <asm/common.lds.S>

  __init_begin = .;
  init.data : { INIT_DATA }
  __init_end = .;

  /*
   * The alignment-specific data sections are named here for the same reason
   * RW_DATA() names them on every other architecture: left out, they are
   * orphans, and where an orphan lands is the linker's choice rather than this
   * script's.
   *
   * The two linkers do not choose alike. bfd ld put them after .data, which is
   * writable and correct. lld puts them with the read-only data, which places
   * them inside __start_rodata..__end_rodata -- so mark_rodata_ro() mprotects
   * bit_wait_table and friends read-only, and the first process to wait on a
   * buffer dies on the write with "Segfault with no mm" and a backtrace
   * pointing at prepare_to_wait, which has nothing to do with the cause.
   *
   * DATA_DATA already claims .data..once, .data..do_once and
   * .data..shared_aligned, so only these need adding.
   */
  .data    :
  {
    INIT_TASK_DATA(KERNEL_STACK_SIZE)
    NOSAVE_DATA
    PAGE_ALIGNED_DATA(PAGE_SIZE)
    CACHELINE_ALIGNED_DATA(L1_CACHE_BYTES)
    READ_MOSTLY_DATA(L1_CACHE_BYTES)
    DATA_DATA
    *(.gnu.linkonce.d*)
    CONSTRUCTORS
  }
  .data1   : { *(.data1) }
  .ctors         :
  {
    *(.ctors)
  }
  .dtors         :
  {
    *(.dtors)
  }

  .got           : { *(.got.plt) *(.got) }
  .eh_frame       : { KEEP (*(.eh_frame)) }
  .dynamic       : { *(.dynamic) }
  .tdata	  : { *(.tdata .tdata.* .gnu.linkonce.td.*) }
  .tbss		  : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata     : { *(.sdata) }
  _edata  =  .;
  PROVIDE (edata = .);
  . = ALIGN(PAGE_SIZE);
  __bss_start = .;
  PROVIDE(_bss_start = .);
  SBSS(0)
  BSS(0)
   __bss_stop = .;
  _end = .;
  PROVIDE (end = .);

  STABS_DEBUG
  DWARF_DEBUG
  MODINFO
  ELF_DETAILS

  DISCARDS
}
