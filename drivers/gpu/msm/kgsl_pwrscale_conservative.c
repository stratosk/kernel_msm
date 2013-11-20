/* Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <mach/socinfo.h>
#include <mach/scm.h>

#include "kgsl.h"
#include "kgsl_pwrscale.h"
#include "kgsl_device.h"

/* 1 = print statistics */
static unsigned int g_show_stats = 0;

/* Polling interval in us */
#define MIN_POLL_INTERVAL 10000
#define MAX_POLL_INTERVAL 1000000
static unsigned long g_polling_interval = 100000;

static unsigned long walltime_total = 0;
static unsigned long busytime_total = 0;

struct gpu_thresh_tbl {
	unsigned int up_threshold;
	unsigned int down_threshold;
};

#define GPU_SCALE(u,d) \
    { \
        .up_threshold = u, \
        .down_threshold = d, \
    }

static struct gpu_thresh_tbl thresh_tbl[] = {
	GPU_SCALE(110, 60),
	GPU_SCALE(90, 45),
	GPU_SCALE(75, 35),
	GPU_SCALE(60, 0),
	GPU_SCALE(100, 0),
};

static void conservative_wake(struct kgsl_device *device,
			      struct kgsl_pwrscale *pwrscale)
{
	struct kgsl_power_stats stats;

	if (g_show_stats == 1)
		pr_info("%s: GPU waking up\n", KGSL_NAME);

	if (device->state != KGSL_STATE_NAP) {
		kgsl_pwrctrl_pwrlevel_change(device,
					     device->pwrctrl.default_pwrlevel);
		/* reset the power stats counters */
		device->ftbl->power_stats(device, &stats);
		walltime_total = 0;
		busytime_total = 0;
	}
}

static void conservative_idle(struct kgsl_device *device,
			      struct kgsl_pwrscale *pwrscale)
{
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;
	struct kgsl_power_stats stats;
	int val = 0;
	unsigned int loadpct;

	device->ftbl->power_stats(device, &stats);

	if (stats.total_time == 0)
		return;

	walltime_total += (unsigned long)stats.total_time;
	busytime_total += (unsigned long)stats.busy_time;

	if (walltime_total <= g_polling_interval)
		return;

	if (g_show_stats == 1)
		pr_info("%s: walltime_total: %lu, busytime_total: %lu\n",
			KGSL_NAME, walltime_total, busytime_total);

	loadpct = (100 * busytime_total) / walltime_total;

	walltime_total = busytime_total = 0;

	if (loadpct < thresh_tbl[pwr->active_pwrlevel].down_threshold)
		val = 1;
	else if (loadpct > thresh_tbl[pwr->active_pwrlevel].up_threshold)
		val = -1;

	if (g_show_stats == 1)
		pr_info("%s: loadpct: %d, active_pwrlevel: %d, change: %d\n",
			KGSL_NAME, loadpct, pwr->active_pwrlevel, val);

	if (val)
		kgsl_pwrctrl_pwrlevel_change(device,
					     pwr->active_pwrlevel + val);
}

static void conservative_busy(struct kgsl_device *device,
			      struct kgsl_pwrscale *pwrscale)
{
	device->on_time = ktime_to_us(ktime_get());
}

static void conservative_sleep(struct kgsl_device *device,
			       struct kgsl_pwrscale *pwrscale)
{
	if (g_show_stats == 1)
		pr_info("%s: GPU going to sleep\n", KGSL_NAME);
}

static ssize_t conservative_stats_show(struct kgsl_device *device,
				       struct kgsl_pwrscale *pwrscale,
				       char *buf)
{
	pr_info("%s: Print statistics: %u\n", KGSL_NAME, g_show_stats);

	return sprintf(buf, "%u\n", g_show_stats);
}

static ssize_t conservative_stats_store(struct kgsl_device *device,
					struct kgsl_pwrscale *pwrscale,
					const char *buf, size_t count)
{
	unsigned int tmp;
	int err;

	err = kstrtoint(buf, 0, &tmp);
	if (err) {
		pr_err("%s: failed setting stats show!\n", KGSL_NAME);
		return err;
	}

	g_show_stats = tmp ? 1 : 0;

	pr_info("%s: Print statistics: %u\n", KGSL_NAME, g_show_stats);

	return count;
}

PWRSCALE_POLICY_ATTR(print_stats, 0644, conservative_stats_show,
		     conservative_stats_store);

static ssize_t conservative_polling_interval_show(struct kgsl_device *device, struct kgsl_pwrscale
						  *pwrscale, char *buf)
{
	return sprintf(buf, "%lu\n", g_polling_interval);
}

static ssize_t conservative_polling_interval_store(struct kgsl_device *device, struct kgsl_pwrscale
						   *pwrscale, const char *buf,
						   size_t count)
{
	unsigned long tmp;
	int err;

	err = kstrtoul(buf, 0, &tmp);
	if (err) {
		pr_err("%s: failed setting new polling interval!\n", KGSL_NAME);
		return err;
	}

	if (tmp < MIN_POLL_INTERVAL)
		tmp = MIN_POLL_INTERVAL;
	else if (tmp > MAX_POLL_INTERVAL)
		tmp = MAX_POLL_INTERVAL;

	g_polling_interval = tmp;

	if (g_show_stats == 1)
		pr_info("%s: new polling interval: %lu\n", KGSL_NAME,
			g_polling_interval);

	return count;
}

PWRSCALE_POLICY_ATTR(polling_interval, 0644, conservative_polling_interval_show,
		     conservative_polling_interval_store);

static ssize_t conservative_threshold_table_show(struct kgsl_device *device, struct kgsl_pwrscale
						 *pwrscale, char *buf)
{
	int i, len = 0;
	struct kgsl_pwrctrl *pwr = &device->pwrctrl;

	if (!buf)
		return -EINVAL;

	for (i = 0; i < pwr->num_pwrlevels; i++) {
		len += sprintf(buf + len, "%d ", i);
		len += sprintf(buf + len, "%3d ", thresh_tbl[i].up_threshold);
		len += sprintf(buf + len, "%2d", thresh_tbl[i].down_threshold);
		len += sprintf(buf + len, "\n");
	}

	return len;
}

static ssize_t conservative_threshold_table_store(struct kgsl_device *device, struct kgsl_pwrscale
						  *pwrscale, const char *buf,
						  size_t count)
{
	int err;
	unsigned int tmp[3];

	err = sscanf(buf, "%d %d %d", &tmp[0], &tmp[1], &tmp[2]);

	if (err != ARRAY_SIZE(tmp))
		return -EINVAL;

	thresh_tbl[tmp[0]].up_threshold = tmp[1];
	thresh_tbl[tmp[0]].down_threshold = tmp[2];

	if (g_show_stats == 1)
		pr_info("%s: level %d new thresholds up: %d, down: %d\n",
			KGSL_NAME, tmp[0], thresh_tbl[tmp[0]].up_threshold,
			thresh_tbl[tmp[0]].down_threshold);

	return err;
}

PWRSCALE_POLICY_ATTR(threshold_table, 0644, conservative_threshold_table_show,
		     conservative_threshold_table_store);

static struct attribute *conservative_attrs[] = {
	&policy_attr_print_stats.attr,
	&policy_attr_polling_interval.attr,
	&policy_attr_threshold_table.attr,
	NULL
};

static struct attribute_group conservative_attr_group = {
	.attrs = conservative_attrs,
};

static int conservative_init(struct kgsl_device *device,
			     struct kgsl_pwrscale *pwrscale)
{
	kgsl_pwrscale_policy_add_files(device, pwrscale,
				       &conservative_attr_group);

	return 0;
}

static void conservative_close(struct kgsl_device *device,
			       struct kgsl_pwrscale *pwrscale)
{
	kgsl_pwrscale_policy_remove_files(device, pwrscale,
					  &conservative_attr_group);
}

struct kgsl_pwrscale_policy kgsl_pwrscale_policy_conservative = {
	.name = "conservative",
	.init = conservative_init,
	.busy = conservative_busy,
	.idle = conservative_idle,
	.sleep = conservative_sleep,
	.wake = conservative_wake,
	.close = conservative_close
};

EXPORT_SYMBOL(kgsl_pwrscale_policy_conservative);
