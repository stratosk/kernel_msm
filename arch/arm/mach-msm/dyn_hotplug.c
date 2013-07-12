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
#define UP_THRESHOLD	(20)
#define MIN_ONLINE	(2)

struct dyn_hp_data {
	unsigned int up_threshold;
	unsigned int delay;
	unsigned int min_online;
	unsigned int down_timer;
	unsigned int up_timer;
	struct delayed_work work;
} *hp_data;

static inline void up_one(void)
{
	/* All CPUs are online, return */
	if (num_online_cpus() == num_possible_cpus())
		return;

	cpu_up(num_online_cpus());

	hp_data->down_timer = 0;
}

static inline void down_one(void)
{
	/* Min online CPUs, return */
	if (num_online_cpus() == hp_data->min_online)
		return;

	cpu_down(num_online_cpus() - 1);

	hp_data->down_timer = 0;
	hp_data->up_timer = 0;
}

static __cpuinit void load_timer(struct work_struct *work)
{
	unsigned int cpu;
	unsigned int avg_load = 0;
	struct cpufreq_policy cpu_policy;

	if (hp_data->down_timer < 10)
		hp_data->down_timer++;

	if (hp_data->up_timer < 2)
		hp_data->up_timer++;

	for_each_online_cpu(cpu) {
		cpufreq_get_policy(&cpu_policy, cpu);
		avg_load += cpu_policy.util;
	}

	avg_load /= num_online_cpus();
	pr_debug("%s: avg_load: %u, num_online_cpus: %u, down_timer: %u",
		__func__, avg_load, num_online_cpus(), hp_data->down_timer);

	if (avg_load >= hp_data->up_threshold && hp_data->up_timer >= 2)
		up_one();
	else if (hp_data->down_timer >= 10)
		down_one();

	schedule_delayed_work(&hp_data->work, hp_data->delay);
}

static int __init dyn_hp_init(void)
{
	hp_data = kzalloc(sizeof(*hp_data), GFP_KERNEL);
	if (!hp_data)
		return -ENOMEM;

	hp_data->delay = DELAY;
	hp_data->up_threshold = UP_THRESHOLD;
	hp_data->min_online = MIN_ONLINE;

	INIT_DELAYED_WORK(&hp_data->work, load_timer);
	schedule_delayed_work(&hp_data->work, hp_data->delay);

	pr_info("%s: activated\n", __func__);

	return 0;
}

static void __exit dyn_hp_exit(void)
{
	cancel_delayed_work(&hp_data->work);
	flush_scheduled_work();
	kfree(hp_data);
	pr_info("%s: deactivated\n", __func__);
}

MODULE_AUTHOR("Stratos Karafotis <stratosk@semaphore.gr");
MODULE_DESCRIPTION("'dyn_hotplug' - A dynamic hotplug driver for mako");
MODULE_LICENSE("GPL");

module_init(dyn_hp_init);
module_exit(dyn_hp_exit);
