/*
 * MSM Hotplug Driver
 *
 * Copyright (C) 2013 Fluxi <linflux@arcor.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <mach/cpufreq.h>

#define MSM_HOTPLUG		"msm-hotplug"
#define HISTORY_SIZE		10

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;

struct cpu_stats {
	unsigned int load_hist[HISTORY_SIZE];
	unsigned int hist_cnt;
	unsigned int total_cpus;
	unsigned int online_cpus;
	unsigned int current_load;
};

static struct cpu_stats stats;

extern unsigned int report_load_at_max_freq(void);

static struct cpu_stats *get_load_stats(void)
{
	unsigned int i, j;
	unsigned int load = 0;
	struct cpu_stats *st = &stats;

	st->load_hist[st->hist_cnt] = report_load_at_max_freq();

	for (i = 0, j = st->hist_cnt; i < HISTORY_SIZE; i++, j--) {
		load += st->load_hist[j];

		if (j == 0)
			j = HISTORY_SIZE;
	}

	if (++st->hist_cnt == HISTORY_SIZE)
		st->hist_cnt = 0;

	st->online_cpus = num_online_cpus();
	st->current_load = load / HISTORY_SIZE;

	return st;
}

EXPORT_SYMBOL_GPL(get_load_stats);

static void msm_hotplug_fn(struct work_struct *work)
{
	unsigned int cur_load, online_cpus;
	struct cpu_stats *st = get_load_stats();

	cur_load = st->current_load;
	online_cpus = st->online_cpus;

	pr_info("%s: cur_load: %u online_cpus %u\n", MSM_HOTPLUG, cur_load,
		online_cpus);

	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, HZ);
}

EXPORT_SYMBOL_GPL(msm_hotplug_fn);

static void msm_hotplug_early_suspend(struct early_suspend *handler)
{
	return;
}

EXPORT_SYMBOL_GPL(msm_hotplug_early_suspend);

static void msm_hotplug_late_resume(struct early_suspend *handler)
{
	return;
}

EXPORT_SYMBOL_GPL(msm_hotplug_late_resume);

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend msm_hotplug_suspend = {
	.suspend = msm_hotplug_early_suspend,
	.resume = msm_hotplug_late_resume,
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN - 1,
};
#endif

static int __init msm_hotplug_init(void)
{
	int ret = 0;
	struct cpu_stats *st = &stats;

	hotplug_wq = alloc_workqueue("msm_hotplug_wq", 0, 0);
	if (!hotplug_wq) {
		pr_err("%s: Creation of hotplug work failed\n", MSM_HOTPLUG);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&hotplug_work, msm_hotplug_fn);
	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, HZ * 20);

	st->total_cpus = NR_CPUS;

#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&msm_hotplug_suspend);
#endif
	return ret;
}

EXPORT_SYMBOL_GPL(msm_hotplug_init);

late_initcall(msm_hotplug_init);

static struct platform_device msm_hotplug_device = {
	.name = MSM_HOTPLUG,
	.id = -1,
};

static int __init msm_hotplug_device_init(void)
{
	int ret;

	ret = platform_device_register(&msm_hotplug_device);
	if (ret) {
		pr_err("%s: Device init failed: %d\n", MSM_HOTPLUG, ret);
		return ret;
	}

	pr_info("%s: Device init\n", MSM_HOTPLUG);

	return ret;
}

EXPORT_SYMBOL_GPL(msm_hotplug_device_init);

late_initcall(msm_hotplug_device_init);

MODULE_AUTHOR("Fluxi <linflux@arcor.de>");
MODULE_DESCRIPTION("MSM Hotplug Driver");
MODULE_LICENSE("GPL");
