/*
 * Low level sleep code for the SoC r8a7740
 *
 * Copyright (C) 2013 Bastian Hecht
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

#ifdef CONFIG_SUSPEND

/* r8a7740_shutdown expects L2 to be flushed */
	.text
ENTRY(r8a7740_shutdown)
	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)		@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	bl      v7_flush_dcache_all

wfi_loop:
	bl	cpu_v7_do_idle
	b wfi_loop
ENDPROC(r8a7740_shutdown)

	.text
ENTRY(v7_cpu_resume)
	bl	v7_invalidate_l1
	b	cpu_resume
ENDPROC(v7_cpu_resume)

/*
 * The entry point of a warm reboot, used by wakeup scenarios
 *
 * The CPU jumps in this case to (0xfffff000 & SBAR), so we need
 * to align this function properly.
 * We use a long jump into the text segment and use the physical
 * address as the MMU is still turned off.
 */
	.align	12
	.text
ENTRY(r8a7740_resume)
	ldr	pc, 1f
1:	.long	v7_cpu_resume - PAGE_OFFSET + PLAT_PHYS_OFFSET
ENDPROC(r8a7740_resume)
#endif
