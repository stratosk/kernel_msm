/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_MMU_H
#define _ASM_ARC_MMU_H

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long asid;	/* Pvt Addr-Space ID for mm */
#ifdef CONFIG_ARC_TLB_DBG
	struct task_struct *tsk;
#endif
} mm_context_t;

struct bcr_mmu_1_2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int ver:8, ways:4, sets:4, u_itlb:8, u_dtlb:8;
#else
	unsigned int u_dtlb:8, u_itlb:8, sets:4, ways:4, ver:8;
#endif
};

struct bcr_mmu_3 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int ver:8, ways:4, sets:4, osm:1, reserv:3, pg_sz:4,
		     u_itlb:4, u_dtlb:4;
#else
	unsigned int u_dtlb:4, u_itlb:4, pg_sz:4, reserv:3, osm:1, sets:4,
		     ways:4, ver:8;
#endif
};

#endif

/* MMU Management regs */
#define ARC_REG_TLBPD0		0x405
#define ARC_REG_TLBPD1		0x406
#define ARC_REG_TLBINDEX	0x407
#define ARC_REG_TLBCOMMAND	0x408
#define ARC_REG_PID		0x409
#define ARC_REG_SCRATCH_DATA0	0x418

/* Bits in MMU PID register */
#define MMU_ENABLE		(1 << 31)	/* Enable MMU for process */

/* Error code if probe fails */
#define TLB_LKUP_ERR		0x80000000

/* TLB Commands */
#define TLBWrite    0x1
#define TLBRead     0x2
#define TLBGetIndex 0x3
#define TLBProbe    0x4

#if (CONFIG_ARC_MMU_VER >= 2)
#define TLBWriteNI  0x5		/* write JTLB without inv uTLBs */
#define TLBIVUTLB   0x6		/* explicitly inv uTLBs */
#else
#undef TLBWriteNI		/* These cmds don't exist on older MMU */
#undef TLBIVUTLB
#endif

#endif
