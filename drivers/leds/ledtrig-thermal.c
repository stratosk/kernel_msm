/*
 * LED Thermal Trigger
 *
 * Copyright (C) 2013 Stratos Karafotis <stratosk@semaphore.gr>
 *
 * Based on Atsushi Nemoto's ledtrig-heartbeat.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#define DEBUG
#define pr_fmt(fmt) "ledtrig_thermal: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/earlysuspend.h>
#include <linux/leds.h>
#include "leds.h"

#define HIGH_TEMP	(62)	/* in Celcius */
#define LOW_TEMP	(52)
#define SENSOR_ID	(7)
#define DELAY_OFF	(5 * HZ)
#define DELAY_ON	(2 * HZ)

static void check_temp(struct work_struct *work);

struct thermal_trig_data {
	unsigned delay;
	int brightness;
	int prev_brightness;
	int active;
	struct delayed_work work;
	struct early_suspend suspend;
};

static struct led_trigger thermal_led_trigger;

static void thermal_trig_early_suspend(struct early_suspend *h)
{
	struct thermal_trig_data *thermal_data =
		container_of(h, struct thermal_trig_data, suspend);

	if (!thermal_data->active)
		return;

	cancel_delayed_work(&thermal_data->work);
	flush_scheduled_work();

	if (thermal_data->brightness)
		led_trigger_event(&thermal_led_trigger, LED_OFF);

	pr_debug("%s: led_br: %u\n", __func__, thermal_data->brightness);
}

static void thermal_trig_late_resume(struct early_suspend *h)
{
	struct thermal_trig_data *thermal_data =
		container_of(h, struct thermal_trig_data, suspend);

	if (!thermal_data->active)
		return;

	thermal_data->delay = (thermal_data->brightness == LED_OFF) ?
						DELAY_OFF : DELAY_ON;
	schedule_delayed_work(&thermal_data->work, thermal_data->delay);

	pr_debug("%s: led_br: %u\n", __func__, thermal_data->brightness);
}

static void thermal_trig_activate(struct led_classdev *led_cdev)
{
	struct thermal_trig_data *thermal_data;

	thermal_data = kzalloc(sizeof(*thermal_data), GFP_KERNEL);
	if (!thermal_data)
		return;

	led_cdev->trigger_data = thermal_data;
	thermal_data->delay = DELAY_OFF;
	thermal_data->active = 1;
	thermal_data->brightness = 0;

	thermal_data->suspend.suspend = thermal_trig_early_suspend;
	thermal_data->suspend.resume =  thermal_trig_late_resume;
	thermal_data->suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
	register_early_suspend(&thermal_data->suspend);

	INIT_DELAYED_WORK(&thermal_data->work, check_temp);
	schedule_delayed_work(&thermal_data->work, thermal_data->delay);

	pr_info("%s: activated\n", __func__);
}

static void thermal_trig_deactivate(struct led_classdev *led_cdev)
{
	struct thermal_trig_data *thermal_data = led_cdev->trigger_data;

	if (thermal_data) {
		cancel_delayed_work(&thermal_data->work);
		flush_scheduled_work();

		thermal_data->active = 0;
		led_set_brightness(led_cdev, LED_OFF);
		unregister_early_suspend(&thermal_data->suspend);
		kfree(thermal_data);
	}

	pr_info("%s: deactivated\n", __func__);
}

static struct led_trigger thermal_led_trigger = {
	.name     = "thermal",
	.activate = thermal_trig_activate,
	.deactivate = thermal_trig_deactivate,
};

static void check_temp(struct work_struct *work)
{
	struct thermal_trig_data *thermal_data =
		container_of(work, struct thermal_trig_data, work.work);

	struct tsens_device tsens_dev;
	unsigned long temp = 0;
	int br = 0;
	int ret;
	int diff;
	int upd = 0;

	tsens_dev.sensor_num = SENSOR_ID;
	ret = tsens_get_temp(&tsens_dev, &temp);
	if (ret) {
		pr_debug("%s: Unable to read TSENS sensor %d\n", __func__,
				tsens_dev.sensor_num);
		goto reschedule;
	}

	/* A..B -> C..D		x' = (D-C)*(X-A)/(B-A) */
	if (temp > LOW_TEMP)
		br = (LED_FULL * (temp - LOW_TEMP)) / (HIGH_TEMP - LOW_TEMP);

	diff = abs(br - thermal_data->brightness);
	if (diff < 10)
		upd = 1;
	else if (diff < 20)
		upd = 2;
	else if (diff < 40)
		upd = 5;
	else if (diff < 120)
		upd = 10;
	else
		upd = diff;

	if (br > thermal_data->brightness)
		thermal_data->brightness += upd;
	else
		thermal_data->brightness -= upd;

	if (thermal_data->brightness < LED_OFF)
		thermal_data->brightness = LED_OFF;
	else if (thermal_data->brightness > LED_FULL)
		thermal_data->brightness = LED_FULL;

	pr_debug("%s: temp: %lu, br: %u, led_br: %u\n", __func__,
					temp, br, thermal_data->brightness);

	if (thermal_data->brightness == thermal_data->prev_brightness)
		goto reschedule;

	thermal_data->prev_brightness = thermal_data->brightness;
	led_trigger_event(&thermal_led_trigger, thermal_data->brightness);

reschedule:
	thermal_data->delay = (thermal_data->brightness == LED_OFF) ?
						DELAY_OFF : DELAY_ON;
	schedule_delayed_work(&thermal_data->work, thermal_data->delay);
}

static int __init thermal_trig_init(void)
{
	return led_trigger_register(&thermal_led_trigger);
}

static void __exit thermal_trig_exit(void)
{
	led_trigger_unregister(&thermal_led_trigger);
}

module_init(thermal_trig_init);
module_exit(thermal_trig_exit);

MODULE_AUTHOR("Stratos Karafotis <stratosk@semaphore.gr>");
MODULE_DESCRIPTION("Thermal LED trigger");
MODULE_LICENSE("GPL");
