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
#define MAX_ONLINE	(4)
#define DOWN_TIMER_CNT	(10)	/* 5 secs */
#define UP_TIMER_CNT	(2)	/* 1 sec */

static int enabled;
static unsigned int up_threshold;
static unsigned int min_online;
static unsigned int max_online;

struct dyn_hp_data {
	unsigned int up_threshold;
	unsigned int delay;
	unsigned int min_online;
	unsigned int max_online;
	unsigned int down_timer;
	unsigned int up_timer;
	unsigned int enabled;
	struct delayed_work work;
	struct early_suspend suspend;
} *hp_data;

/*
 * Bring online each possible CPU up to max_online threshold if lim is true or
 * up to num_possible_cpus if lim is false
 */
static inline void up_all(bool lim)
{
	unsigned int cpu;
	unsigned int max = (lim ? hp_data->max_online : num_possible_cpus());

	for_each_possible_cpu(cpu)
		if (!cpu_online(cpu) && num_online_cpus() < max)
			cpu_up(cpu);

	hp_data->down_timer = 0;
}

/* Bring offline each possible CPU down to min_online threshold */
static inline void down_all(void)
{
	unsigned int cpu;

	for_each_online_cpu(cpu)
		if (cpu && num_online_cpus() > hp_data->min_online)
			cpu_down(cpu);
}

static void hp_early_suspend(struct early_suspend *h)
{
	pr_debug("%s: num_online_cpus: %u\n", __func__, num_online_cpus());
	return;
}

/* On late resume bring online all CPUs to prevent lags */
static __cpuinit void hp_late_resume(struct early_suspend *h)
{
	pr_debug("%s: num_online_cpus: %u\n", __func__, num_online_cpus());

	up_all(true);
}

/* Iterate through possible CPUs and bring online the first found offline one */
static inline void up_one(void)
{
	unsigned int cpu;

	/* All CPUs are online, return */
	if (num_online_cpus() == num_possible_cpus() ||
		num_online_cpus() == hp_data->max_online)
		return;

	for_each_possible_cpu(cpu)
		if (cpu && !cpu_online(cpu)) {
			cpu_up(cpu);

			hp_data->down_timer = 0;
			hp_data->up_timer = 0;

			break;
		}
}

/* Iterate through online CPUs and bring online the first one */
static inline void down_one(void)
{
	unsigned int cpu;
	unsigned int l_cpu = 0;
	unsigned int l_freq = ~0;

	/* Min online CPUs, return */
	if (num_online_cpus() == hp_data->min_online)
		return;

	get_online_cpus();

	for_each_online_cpu(cpu)
		if (cpu) {
			unsigned int cur = cpufreq_quick_get(cpu);

			if (l_freq > cur) {
				l_freq = cur;
				l_cpu = cpu;
			}
		}	
	put_online_cpus();

	cpu_down(l_cpu);
	hp_data->down_timer = 0;
	hp_data->up_timer = 0;
}

/*
 * Every DELAY, check the average load of online CPUs. If the average load
 * is above up_threshold bring online one more CPU if up_timer has expired.
 * If the average load is below up_threshold offline one more CPU if the
 * down_timer has expired.
 */
static __cpuinit void load_timer(struct work_struct *work)
{
	unsigned int cpu;
	unsigned int avg_load = 0;

	if (hp_data->down_timer < DOWN_TIMER_CNT)
		hp_data->down_timer++;

	if (hp_data->up_timer < UP_TIMER_CNT)
		hp_data->up_timer++;

	for_each_online_cpu(cpu)
		avg_load += cpufreq_quick_get_util(cpu);

	avg_load /= num_online_cpus();
	pr_debug("%s: avg_load: %u, num_online_cpus: %u, down_timer: %u\n",
		__func__, avg_load, num_online_cpus(), hp_data->down_timer);

	if (avg_load >= hp_data->up_threshold &&
					hp_data->up_timer >= UP_TIMER_CNT)
		up_one();
	else if (hp_data->down_timer >= DOWN_TIMER_CNT)
		down_one();

	schedule_delayed_work_on(0, &hp_data->work, hp_data->delay);
}

static void dyn_hp_enable(void)
{
	if (hp_data->enabled)
		return;
	schedule_delayed_work_on(0, &hp_data->work, hp_data->delay);
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

	/* Driver is disabled bring online all CPUs unconditionally */
	up_all(false);
	hp_data->enabled = 0;
}

/******************** Module parameters *********************/

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
	unsigned int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	if (i < 1 || i > 100)
		return -EINVAL;

	ret = param_set_uint(val, kp);
	if (ret == 0)
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
	unsigned int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	if (i < 1 || i > hp_data->max_online || i > num_possible_cpus())
		return -EINVAL;

	ret = param_set_uint(val, kp);
	if (ret == 0) {
		hp_data->min_online = min_online;
		if (hp_data->enabled)
			up_all(true);
	}
	return ret;
}

static struct kernel_param_ops min_online_ops = {
	.set = set_min_online,
	.get = param_get_uint,
};

module_param_cb(min_online, &min_online_ops, &min_online, 0644);

static __cpuinit int set_max_online(const char *val,
						const struct kernel_param *kp)
{
	int ret = 0;
	unsigned int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	if (i < 1 || i < hp_data->min_online || i > num_possible_cpus())
		return -EINVAL;

	ret = param_set_uint(val, kp);
	if (ret == 0) {
		hp_data->max_online = max_online;
		if (hp_data->enabled) {
			down_all();
			up_all(true);
		}
	}
	return ret;
}

static struct kernel_param_ops max_online_ops = {
	.set = set_max_online,
	.get = param_get_uint,
};

module_param_cb(max_online, &max_online_ops, &max_online, 0644);

/***************** end of module parameters *****************/

static int __init dyn_hp_init(void)
{
	hp_data = kzalloc(sizeof(*hp_data), GFP_KERNEL);
	if (!hp_data)
		return -ENOMEM;

	hp_data->delay = DELAY;
	hp_data->up_threshold = UP_THRESHOLD;
	hp_data->min_online = MIN_ONLINE;
	hp_data->max_online = MAX_ONLINE;

	hp_data->suspend.suspend = hp_early_suspend;
	hp_data->suspend.resume =  hp_late_resume;
	hp_data->suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	hp_data->enabled = 1;
	up_threshold = hp_data->up_threshold;
	enabled = hp_data->enabled;
	min_online = hp_data->min_online;
	max_online = hp_data->max_online;
	register_early_suspend(&hp_data->suspend);

	INIT_DELAYED_WORK(&hp_data->work, load_timer);
	schedule_delayed_work_on(0, &hp_data->work, hp_data->delay);

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
