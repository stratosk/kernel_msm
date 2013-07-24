/*
 * Dynamic Hotplug for mako
 *
 * Copyright (C) 2013 Stratos Karafotis <stratosk@semaphore.gr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define DEBUG
#define pr_fmt(fmt) "dyn_hotplug: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/earlysuspend.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define DELAY		(HZ / 2)
#define UP_THRESHOLD	(25)
#define MIN_ONLINE	(2)

static int enabled;
static unsigned int up_threshold;
static unsigned int min_online;

struct dyn_hp_data {
	unsigned int up_threshold;
	unsigned int delay;
	unsigned int min_online;
	unsigned int down_timer;
	unsigned int up_timer;
	unsigned int enabled;
	struct delayed_work work;
	struct early_suspend suspend;
} *hp_data;

static inline void up_all(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		if (!cpu_online(cpu))
			cpu_up(cpu);

	hp_data->down_timer = 0;
}

static void hp_early_suspend(struct early_suspend *h)
{
	pr_debug("%s: num_online_cpus: %u\n", __func__, num_online_cpus());
	return;
}

static __cpuinit void hp_late_resume(struct early_suspend *h)
{
	pr_debug("%s: num_online_cpus: %u\n", __func__, num_online_cpus());

	up_all();
}

static inline void up_one(void)
{
	unsigned int cpu;

	/* All CPUs are online, return */
	if (num_online_cpus() == num_possible_cpus())
		return;

	for_each_possible_cpu(cpu)
		if (cpu && !cpu_online(cpu)) {
			cpu_up(cpu);

			hp_data->down_timer = 0;
			hp_data->up_timer = 0;

			break;
		}
}

static inline void down_one(void)
{
	unsigned int cpu;

	/* Min online CPUs, return */
	if (num_online_cpus() == hp_data->min_online)
		return;

	for_each_online_cpu(cpu)
		if (cpu) {
			cpu_down(cpu);

			hp_data->down_timer = 0;
			hp_data->up_timer = 0;

			break;
		}
}

static __cpuinit void load_timer(struct work_struct *work)
{
	unsigned int cpu;
	unsigned int avg_load = 0;

	if (hp_data->down_timer < 10)
		hp_data->down_timer++;

	if (hp_data->up_timer < 2)
		hp_data->up_timer++;

	for_each_online_cpu(cpu)
		avg_load += cpufreq_quick_get_util(cpu);

	avg_load /= num_online_cpus();
	pr_debug("%s: avg_load: %u, num_online_cpus: %u, down_timer: %u\n",
		__func__, avg_load, num_online_cpus(), hp_data->down_timer);

	if (avg_load >= hp_data->up_threshold && hp_data->up_timer >= 2)
		up_one();
	else if (hp_data->down_timer >= 10)
		down_one();

	schedule_delayed_work(&hp_data->work, hp_data->delay);
}

static void dyn_hp_enable(void)
{
	if (hp_data->enabled)
		return;
	schedule_delayed_work(&hp_data->work, hp_data->delay);
	register_early_suspend(&hp_data->suspend);
	hp_data->enabled = 1;
}

static void dyn_hp_disable(void)
{
	if (!hp_data->enabled)
		return;
	cancel_delayed_work(&hp_data->work);
	flush_scheduled_work();
	unregister_early_suspend(&hp_data->suspend);

	up_all();
	hp_data->enabled = 0;
}

static __cpuinit int set_enabled(const char *val, const struct kernel_param *kp)
{
	int ret = 0;

	ret = param_set_bool(val, kp);
	if (!enabled)
		dyn_hp_disable();
	else
		dyn_hp_enable();

	pr_info("%s: enabled = %d\n", __func__, enabled);
	return ret;
}

static struct kernel_param_ops enabled_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &enabled_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "control dyn_hotplug");

static int set_up_threshold(const char *val, const struct kernel_param *kp)
{
	int ret = 0;

	ret = param_set_uint(val, kp);
	hp_data->up_threshold = up_threshold;
	return ret;
}

static struct kernel_param_ops up_threshold_ops = {
	.set = set_up_threshold,
	.get = param_get_uint,
};

module_param_cb(up_threshold, &up_threshold_ops, &up_threshold, 0644);

static __cpuinit int set_min_online(const char *val,
						const struct kernel_param *kp)
{
	int ret = 0;

	ret = param_set_uint(val, kp);
	hp_data->min_online = min_online;
	up_all();
	return ret;
}

static struct kernel_param_ops min_online_ops = {
	.set = set_min_online,
	.get = param_get_uint,
};

module_param_cb(min_online, &min_online_ops, &min_online, 0644);

static int __init dyn_hp_init(void)
{
	hp_data = kzalloc(sizeof(*hp_data), GFP_KERNEL);
	if (!hp_data)
		return -ENOMEM;

	hp_data->delay = DELAY;
	hp_data->up_threshold = UP_THRESHOLD;
	hp_data->min_online = MIN_ONLINE;

	hp_data->suspend.suspend = hp_early_suspend;
	hp_data->suspend.resume =  hp_late_resume;
	hp_data->suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	hp_data->enabled = 1;
	up_threshold = hp_data->up_threshold;
	enabled = hp_data->enabled;
	min_online = hp_data->min_online;
	register_early_suspend(&hp_data->suspend);

	INIT_DELAYED_WORK(&hp_data->work, load_timer);
	schedule_delayed_work(&hp_data->work, hp_data->delay);

	pr_info("%s: activated\n", __func__);

	return 0;
}

static void __exit dyn_hp_exit(void)
{
	cancel_delayed_work(&hp_data->work);
	flush_scheduled_work();
	unregister_early_suspend(&hp_data->suspend);

	kfree(hp_data);
	pr_info("%s: deactivated\n", __func__);
}

MODULE_AUTHOR("Stratos Karafotis <stratosk@semaphore.gr");
MODULE_DESCRIPTION("'dyn_hotplug' - A dynamic hotplug driver for mako");
MODULE_LICENSE("GPL");

module_init(dyn_hp_init);
module_exit(dyn_hp_exit);
