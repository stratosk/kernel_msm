/*
 * CPUIdle code for SoC r8a7740
 *
 * Copyright (C) 2013 Bastian Hecht
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/cpuidle.h>
#include <linux/module.h>
#include <asm/cpuidle.h>
#include <mach/common.h>
#include <mach/r8a7740.h>

#if defined(CONFIG_SUSPEND) && defined(CONFIG_CPU_IDLE)
static int r8a7740_enter_a3sm_pll_on(struct cpuidle_device *dev,
					struct cpuidle_driver *drv, int index)
{
	r8a7740_enter_a3sm_common(1);
	return 1;
}

static int r8a7740_enter_a3sm_pll_off(struct cpuidle_device *dev,
					struct cpuidle_driver *drv, int index)
{
	r8a7740_enter_a3sm_common(0);
	return 2;
}

static struct cpuidle_driver r8a7740_cpuidle_driver = {
	.name			= "r8a7740_cpuidle",
	.owner			= THIS_MODULE,
	.en_core_tk_irqen	= 1,
	.state_count		= 3,
	.safe_state_index	= 0, /* C1 */
	.states[0] = ARM_CPUIDLE_WFI_STATE,
	.states[1] = {
		.name = "C2",
		.desc = "A3SM PLL ON",
		.exit_latency = 40,
		.target_residency = 30 + 40,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = r8a7740_enter_a3sm_pll_on,
	},
	.states[2] = {
		.name = "C3",
		.desc = "A3SM PLL OFF",
		.exit_latency = 120,
		.target_residency = 30 + 120,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.enter = r8a7740_enter_a3sm_pll_off,
	},
};

void r8a7740_cpuidle_init(void)
{
	shmobile_cpuidle_set_driver(&r8a7740_cpuidle_driver);
}
#else
void r8a7740_cpuidle_init(void) {}
#endif
