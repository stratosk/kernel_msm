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

#define MSM_HOTPLUG	"msm-hotplug"

static struct workqueue_struct *hotplug_wq;
static struct delayed_work hotplug_work;

static void msm_hotplug_fn(struct work_struct *work)
{
	pr_info("%s: %s run\n", MSM_HOTPLUG, __func__);
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

	hotplug_wq = alloc_workqueue("msm_hotplug_wq", 0, 0);
	if (!hotplug_wq) {
		pr_err("%s: Creation of hotplug work failed\n", MSM_HOTPLUG);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&hotplug_work, msm_hotplug_fn);
	queue_delayed_work_on(0, hotplug_wq, &hotplug_work, HZ * 20);

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
