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
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <mach/cpufreq.h>

#define MSM_HOTPLUG		"msm_hotplug"
#define DEFAULT_UPDATE_RATE	HZ / 10
#define START_DELAY		HZ * 20
#define NUM_LOAD_LEVELS		5
#define DEFAULT_HISTORY_SIZE	10
#define DEFAULT_DOWN_LOCK_DUR	2000
#define DEFAULT_SUSPEND_FREQ	702000

static unsigned int debug = 0;
module_param_named(debug_mask, debug, uint, 0644);

#define dprintk(msg...)		\
do { 				\
	if (debug)		\
		pr_info(msg);	\
} while (0)

static struct cpu_hotplug {
	unsigned int suspend_freq;
	atomic_t target_cpus;
	atomic_t down_lock;
	unsigned int down_lock_dur;
	struct work_struct up_work;
	struct work_struct down_work;
	struct timer_list lock_timer;
} hotplug = {
	.suspend_freq = DEFAULT_SUSPEND_FREQ,
	.down_lock = ATOMIC_INIT(0),
	.down_lock_dur = DEFAULT_DOWN_LOCK_DUR
};

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;

static struct cpu_stats {
	unsigned int update_rate;
	unsigned int *load_hist;
	unsigned int hist_size;
	unsigned int hist_cnt;
	unsigned int min_cpus;
	unsigned int total_cpus;
	unsigned int online_cpus;
	unsigned int current_load;
} stats = {
	.update_rate = DEFAULT_UPDATE_RATE,
	.hist_size = DEFAULT_HISTORY_SIZE,
	.min_cpus = 1,
	.total_cpus = NR_CPUS
};

static DEFINE_SPINLOCK(stats_lock);
static DEFINE_MUTEX(hotplug_lock);
extern unsigned int report_load_at_max_freq(void);

static struct cpu_stats *get_load_stats(void)
{
	unsigned int i, j;
	unsigned int load = 0;
	unsigned long flags;
	struct cpu_stats *st = &stats;

	spin_lock_irqsave(&stats_lock, flags);
	st->load_hist[st->hist_cnt] = report_load_at_max_freq();

	for (i = 0, j = st->hist_cnt; i < st->hist_size; i++, j--) {
		load += st->load_hist[j];

		if (j == 0)
			j = st->hist_size;
	}

	if (++st->hist_cnt == st->hist_size)
		st->hist_cnt = 0;

	st->online_cpus = num_online_cpus();
	st->current_load = load / st->hist_size;
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

static void apply_down_lock(void)
{
	struct cpu_hotplug *hp = &hotplug;

	atomic_set(&hp->down_lock, 1);
	mod_timer(&hp->lock_timer,
		  jiffies + msecs_to_jiffies(hp->down_lock_dur));
}

EXPORT_SYMBOL_GPL(apply_down_lock);

static void handle_lock_timer(unsigned long data)
{
	struct cpu_hotplug *hp = &hotplug;

	atomic_set(&hp->down_lock, 0);
}

EXPORT_SYMBOL_GPL(handle_lock_timer);

static void __ref cpu_up_work(struct work_struct *work)
{
	int cpu;
	int target;
	struct cpu_hotplug *hp = &hotplug;

	target = atomic_read(&hp->target_cpus);

	for_each_cpu_not(cpu, cpu_online_mask) {
		if (target == num_online_cpus())
			break;
		if (cpu == 0)
			continue;
		cpu_up(cpu);
	}
}

EXPORT_SYMBOL_GPL(cpu_up_work);

static void cpu_down_work(struct work_struct *work)
{
	int cpu;
	int lock, target;
	struct cpu_hotplug *hp = &hotplug;

	lock = atomic_read(&hp->down_lock);
	if (lock)
		return;

	target = atomic_read(&hp->target_cpus);

	for_each_online_cpu(cpu) {
		if (cpu == 0)
			continue;
		cpu_down(cpu);
		if (target == num_online_cpus())
			break;
	}
}

EXPORT_SYMBOL_GPL(cpu_down_work);

static void online_cpu(unsigned int target)
{
	struct cpu_hotplug *hp = &hotplug;

	atomic_set(&hp->target_cpus, target);
	apply_down_lock();
	queue_work_on(0, hotplug_wq, &hp->up_work);
}

EXPORT_SYMBOL_GPL(online_cpu);

static void offline_cpu(unsigned int target)
{
	struct cpu_hotplug *hp = &hotplug;

	atomic_set(&hp->target_cpus, target);
	queue_work_on(0, hotplug_wq, &hp->down_work);
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
	unsigned int cur_load, online_cpus, target = 0;
	unsigned int i;
	struct cpu_stats *st = get_load_stats();

	cur_load = st->current_load;
	online_cpus = st->online_cpus;

	if (online_cpus == st->min_cpus && mako_boosted) {
		dprintk("%s: cur_load: %3u online_cpus: %u mako_boosted\n",
			MSM_HOTPLUG, cur_load, online_cpus);
		online_cpu(st->min_cpus + 1);
		reschedule_hotplug_fn(st);
		return;
	}

	for (i = st->min_cpus; i < NUM_LOAD_LEVELS; i++) {
		if (cur_load <= load[i].up_threshold
		    && cur_load > load[i].down_threshold) {
			target = i;
			break;
		}
	}

	if (online_cpus != target) {
		if (target > online_cpus)
			online_cpu(target);
		else if (target < online_cpus)
			offline_cpu(target);
	}

	dprintk("%s: cur_load: %3u online_cpus: %u target: %u\n", MSM_HOTPLUG,
		cur_load, online_cpus, target);

	reschedule_hotplug_fn(st);
}

EXPORT_SYMBOL_GPL(msm_hotplug_fn);

static void msm_hotplug_early_suspend(struct early_suspend *handler)
{
	unsigned int cpu = 0;
	struct cpu_hotplug *hp = &hotplug;
	struct cpu_stats *st = &stats;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return;

	atomic_set(&hp->down_lock, 0);
	offline_cpu(st->min_cpus);

	flush_workqueue(hotplug_wq);
	cancel_delayed_work_sync(&hotplug_work);

	msm_cpufreq_set_freq_limits(0, policy->min, hp->suspend_freq);
	pr_info("%s: Early suspend - max freq: %dMHz\n", MSM_HOTPLUG,
		hp->suspend_freq / 1000);
}

EXPORT_SYMBOL_GPL(msm_hotplug_early_suspend);

static void msm_hotplug_late_resume(struct early_suspend *handler)
{
	unsigned int cpu = 0;
	struct cpu_stats *st = &stats;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return;

	online_cpu(st->total_cpus);

	for_each_possible_cpu(cpu)
	    msm_cpufreq_set_freq_limits(cpu, policy->min, policy->max);

	pr_info("%s: Late resume - restore max frequency: %dMHz\n",
		MSM_HOTPLUG, policy->max / 1000);

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

/************************** sysfs interface ************************/

static ssize_t show_suspend_freq(struct device *dev,
				 struct device_attribute *msm_hotplug_attrs,
				 char *buf)
{
	struct cpu_hotplug *hp = &hotplug;

	return sprintf(buf, "%u\n", hp->suspend_freq);
}

static ssize_t store_suspend_freq(struct device *dev,
				  struct device_attribute *msm_hotplug_attrs,
				  const char *buf, size_t count)
{
	unsigned int cpu = 0;
	unsigned int ret, val = 0;
	struct cpu_hotplug *hp = &hotplug;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return -EINVAL;

	ret = sscanf(buf, "%u", &val);
	if (ret != 1 || val < policy->min || val > policy->max)
		return -EINVAL;

	hp->suspend_freq = val;

	return ret;
}

static ssize_t show_down_lock_duration(struct device *dev, struct device_attribute
				       *msm_hotplug_attrs, char *buf)
{
	struct cpu_hotplug *hp = &hotplug;

	return sprintf(buf, "%u\n", hp->down_lock_dur);
}

static ssize_t store_down_lock_duration(struct device *dev, struct device_attribute
					*msm_hotplug_attrs, const char *buf,
					size_t count)
{
	unsigned int ret;
	struct cpu_hotplug *hp = &hotplug;

	ret = sscanf(buf, "%u", &hp->suspend_freq);
	if (ret != 1)
		return -EINVAL;

	return ret;
}

static ssize_t show_update_rate(struct device *dev,
				struct device_attribute *msm_hotplug_attrs,
				char *buf)
{
	struct cpu_stats *st = &stats;

	return sprintf(buf, "%u\n", st->update_rate);
}

static ssize_t store_update_rate(struct device *dev,
				 struct device_attribute *msm_hotplug_attrs,
				 const char *buf, size_t count)
{
	unsigned int ret;
	struct cpu_stats *st = &stats;

	ret = sscanf(buf, "%u", &st->update_rate);
	if (ret != 1)
		return -EINVAL;

	return ret;
}

static ssize_t show_load_levels(struct device *dev,
				struct device_attribute *msm_hotplug_attrs,
				char *buf)
{
	unsigned int i;
	int len = 0;

	if (!buf)
		return -EINVAL;

	for (i = 0; i < NUM_LOAD_LEVELS; i++) {
		len += sprintf(buf + len, "%u ", i);
		len += sprintf(buf + len, "%u ", load[i].up_threshold);
		len += sprintf(buf + len, "%u", load[i].down_threshold);
		len += sprintf(buf + len, "\n");
	}

	return len;
}

static ssize_t store_load_levels(struct device *dev,
				 struct device_attribute *msm_hotplug_attrs,
				 const char *buf, size_t count)
{
	int ret;
	unsigned int val[3];

	ret = sscanf(buf, "%u %u %u", &val[0], &val[1], &val[2]);
	if (ret != ARRAY_SIZE(val))
		return -EINVAL;

	load[val[0]].up_threshold = val[1];
	load[val[0]].down_threshold = val[2];

	return ret;
}

static ssize_t show_current_load(struct device *dev,
				 struct device_attribute *msm_hotplug_attrs,
				 char *buf)
{
	struct cpu_stats *st = &stats;

	return sprintf(buf, "%u\n", st->current_load);
}

static DEVICE_ATTR(suspend_freq, 644, show_suspend_freq, store_suspend_freq);
static DEVICE_ATTR(down_lock_duration, 644, show_down_lock_duration,
		   store_down_lock_duration);
static DEVICE_ATTR(update_rate, 644, show_update_rate, store_update_rate);
static DEVICE_ATTR(load_levels, 644, show_load_levels, store_load_levels);
static DEVICE_ATTR(current_load, 444, show_current_load, NULL);

static struct attribute *msm_hotplug_attrs[] = {
	&dev_attr_suspend_freq.attr,
	&dev_attr_down_lock_duration.attr,
	&dev_attr_update_rate.attr,
	&dev_attr_load_levels.attr,
	&dev_attr_current_load.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = msm_hotplug_attrs,
};

/************************** sysfs end ************************/

static int __init msm_hotplug_init(void)
{
	int ret = 0;
	struct cpu_stats *st = &stats;
	struct cpu_hotplug *hp = &hotplug;

	hotplug_wq = alloc_workqueue("msm_hotplug_wq", 0, 0);
	if (!hotplug_wq) {
		pr_err("%s: Creation of hotplug work failed\n", MSM_HOTPLUG);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&hotplug_work, msm_hotplug_fn);
	INIT_WORK(&hp->up_work, cpu_up_work);
	INIT_WORK(&hp->down_work, cpu_down_work);

	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, START_DELAY);

	st->load_hist = kmalloc(sizeof(st->hist_size), GFP_KERNEL);
	if (!st->load_hist)
		return -ENOMEM;

	setup_timer(&hp->lock_timer, handle_lock_timer, 0);
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
	struct kobject *module_kobj;

	ret = platform_device_register(&msm_hotplug_device);
	if (ret) {
		pr_err("%s: Device init failed: %d\n", MSM_HOTPLUG, ret);
		return ret;
	}

	module_kobj = kset_find_obj(module_kset, MSM_HOTPLUG);
	if (!module_kobj) {
		pr_err("%s: Cannot find kobject for module\n", MSM_HOTPLUG);
		return -ENOENT;
	}

	ret = sysfs_create_group(module_kobj, &attr_group);
	if (ret)
		return pr_err("%s: Creation of sysfs: %d\n", MSM_HOTPLUG, ret);

	pr_info("%s: Device init\n", MSM_HOTPLUG);

	return ret;
}

EXPORT_SYMBOL_GPL(msm_hotplug_device_init);

static void __exit msm_hotplug_device_exit(void)
{
	struct cpu_hotplug *hp = &hotplug;
	struct cpu_stats *st = &stats;

	del_timer(&hp->lock_timer);
	kfree(st->load_hist);
}

EXPORT_SYMBOL_GPL(msm_hotplug_device_init);

late_initcall(msm_hotplug_device_init);
module_exit(msm_hotplug_device_exit);

MODULE_AUTHOR("Fluxi <linflux@arcor.de>");
MODULE_DESCRIPTION("MSM Hotplug Driver");
MODULE_LICENSE("GPL");
