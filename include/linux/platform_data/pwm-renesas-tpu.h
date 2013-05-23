#ifndef __PWM_RENESAS_TPU_H__
#define __PWM_RENESAS_TPU_H__

#define TPU_CHANNEL_MAX		4

#define TPU_PWM_ID(device, channel) \
	((device) * TPU_CHANNEL_MAX + (channel))

struct tpu_pwm_channel_data {
	bool enabled;
	bool active_low;
};

struct tpu_pwm_platform_data {
	struct tpu_pwm_channel_data channels[TPU_CHANNEL_MAX];
};

#endif /* __PWM_RENESAS_TPU_H__ */
