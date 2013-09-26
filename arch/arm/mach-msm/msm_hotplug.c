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
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <mach/cpufreq.h>

#define MSM_HOTPLUG		"msm-hotplug"
#define DEFAULT_UPDATE_RATE	HZ / 10
#define START_DELAY		HZ * 20
#define HISTORY_SIZE		10
#define DEFAULT_DOWN_LOCK_DUR	2000
#define DEFAULT_SUSPEND_FREQ	702000

static unsigned int suspend_freq;
static unsigned int down_lock;
static unsigned int down_lock_dur;
static unsigned int debug;
module_param_named(debug_mask, debug, uint, 0644);

#define dprintk(msg...)		\
do { 				\
	if (debug)		\
		pr_info(msg);	\
} while (0)

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;
static struct timer_list lock_timer;

struct cpu_stats {
	unsigned int update_rate;
	unsigned int load_hist[HISTORY_SIZE];
	unsigned int hist_cnt;
	unsigned int total_cpus;
	unsigned int online_cpus;
	unsigned int current_load;
};

static struct cpu_stats stats;

static DEFINE_SPINLOCK(stats_lock);
extern unsigned int report_load_at_max_freq(void);

static struct cpu_stats *get_load_stats(void)
{
	unsigned int i, j;
	unsigned int load = 0;
	unsigned long flags;
	struct cpu_stats *st = &stats;

	spin_lock_irqsave(&stats_lock, flags);
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
	spin_unlock_irqrestore(&stats_lock, flags);

	return st;
}

EXPORT_SYMBOL_GPL(get_load_stats);

struct load_thresh_tbl {
	unsigned int up_threshold;
	unsigned int down_threshold;
};

#define LOAD_SCALE(u, d)             \
{                          	     \
		.up_threshold = u,   \
		.down_threshold = d, \
}

static struct load_thresh_tbl load[] = {
	LOAD_SCALE(400, 0),
	LOAD_SCALE(50, 0),
	LOAD_SCALE(100, 40),
	LOAD_SCALE(150, 80),
	LOAD_SCALE(410, 140),
};

static void apply_down_lock(unsigned int duration)
{
	down_lock = 1;
	mod_timer(&lock_timer, jiffies + msecs_to_jiffies(duration));
}

EXPORT_SYMBOL_GPL(apply_down_lock);

static void handle_lock_timer(unsigned long data)
{
	down_lock = 0;
}

EXPORT_SYMBOL_GPL(handle_lock_timer);

static void __ref online_cpu(void)
{
	int cpu;

	for_each_cpu_not(cpu, cpu_online_mask) {
		if (cpu == 0)
			continue;
		cpu_up(cpu);
		break;
	}
}

EXPORT_SYMBOL_GPL(online_cpu);

static void offline_cpu(void)
{
	int cpu;

	if (down_lock)
		return;

	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		cpu_down(cpu);
		break;
	}
}

EXPORT_SYMBOL_GPL(offline_cpu);

static int reschedule_hotplug_fn(struct cpu_stats *st)
{
	return queue_delayed_work_on(0, hotplug_wq, &hotplug_work,
				     st->update_rate);
}

EXPORT_SYMBOL_GPL(reschedule_hotplug_fn);

static void msm_hotplug_fn(struct work_struct *work)
{
	unsigned int cur_load, online_cpus;
	struct cpu_stats *st = get_load_stats();

	cur_load = st->current_load;
	online_cpus = st->online_cpus;

	dprintk("%s: cur_load: %3u online_cpus: %u\n", MSM_HOTPLUG, cur_load,
		online_cpus);

	if (online_cpus == 1 && mako_boosted) {
		online_cpu();
		apply_down_lock(down_lock_dur);
		reschedule_hotplug_fn(st);
		return;
	}

	if (cur_load >= load[online_cpus].up_threshold) {
		online_cpu();
		apply_down_lock(down_lock_dur);
	} else if (cur_load < load[online_cpus].down_threshold) {
		offline_cpu();
	}

	reschedule_hotplug_fn(st);
}

EXPORT_SYMBOL_GPL(msm_hotplug_fn);

static void msm_hotplug_early_suspend(struct early_suspend *handler)
{
	unsigned int cpu = 0;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return;

	flush_workqueue(hotplug_wq);
	cancel_delayed_work_sync(&hotplug_work);

	msm_cpufreq_set_freq_limits(0, policy->min, suspend_freq);
	pr_info("%s: Early suspend - max freq: %dMHz\n", "msm_hotplug",
		suspend_freq / 1000);
}

EXPORT_SYMBOL_GPL(msm_hotplug_early_suspend);

static void msm_hotplug_late_resume(struct early_suspend *handler)
{
	unsigned int cpu = 0;
	struct cpu_stats *st = &stats;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return;

	for_each_possible_cpu(cpu)
	    msm_cpufreq_set_freq_limits(cpu, policy->min, policy->max);

	pr_info("mako_hotplug: Late resume - restore max frequency: %dMHz\n",
		policy->max / 1000);

	reschedule_hotplug_fn(st);
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
	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, START_DELAY);

	st->total_cpus = NR_CPUS;
	st->update_rate = DEFAULT_UPDATE_RATE;
	down_lock = 0;
	down_lock_dur = DEFAULT_DOWN_LOCK_DUR;
	debug = 0;
	suspend_freq = DEFAULT_SUSPEND_FREQ;

	setup_timer(&lock_timer, handle_lock_timer, 0);
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

EXPORT_SYMBOL_GPL(msm_hotplug_exit);

static void __exit msm_hotplug_exit(void)
{
	del_timer(&lock_timer);
}

EXPORT_SYMBOL_GPL(msm_hotplug_device_init);

late_initcall(msm_hotplug_device_init);
module_exit(msm_hotplug_exit);

MODULE_AUTHOR("Fluxi <linflux@arcor.de>");
MODULE_DESCRIPTION("MSM Hotplug Driver");
MODULE_LICENSE("GPL");
