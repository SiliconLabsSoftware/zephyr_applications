/*
 * Copyright (c) 2025 Silicon Laboratories Inc. www.silabs.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT resistive_touchscreen_4wire

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_resistive_touchscreen_4wire.h>
#include <zephyr/dt-bindings/input/input-resistive-touchscreen-4wire.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(resistive_touchscreen_4wire, 3);
#define NUMSAMPLES  6

struct adc_channel_config {
  struct adc_dt_spec adc;
  int16_t out_min;
  int16_t out_max;
  uint16_t wire;
  bool invert;
};

struct adc_channel_data {
  int last_out;
};

struct touchscreen_config {
  uint32_t poll_period_ms;
  uint32_t h_res;
  uint32_t v_res;
  uint16_t r_xplate;
  float r_touch_thres;
  bool xy_swap;
  struct gpio_dt_spec xm_pin;
  struct gpio_dt_spec xp_pin;
  struct gpio_dt_spec ym_pin;
  struct gpio_dt_spec yp_pin;
  const struct adc_channel_config *channel_cfg;
  struct adc_channel_data *channel_data;
  struct resistive_touchscreen_4wire_calibration *calibration;
  const uint8_t num_channels;
};

struct touchscreen_data {
  struct k_mutex cal_lock;
  resistive_touchscreen_4wire_raw_data_t raw_data_cb;
  struct k_timer timer;
  struct k_thread thread;

  K_KERNEL_STACK_MEMBER(thread_stack,
                        CONFIG_INPUT_RESISTIVE_TOUCHSCREEN_4WIRE_THREAD_STACK_SIZE);
  bool pressed_old;
};

struct touchscreen_adc_channels {
  const struct adc_channel_config *xm_cfg;
  const struct adc_channel_config *yp_cfg;
};

int resistive_touchscreen_4wire_calibration_get(const struct device *dev,
                                                struct resistive_touchscreen_4wire_calibration *out_cal)
{
  const struct touchscreen_config *cfg = dev->config;
  struct touchscreen_data *data = dev->data;
  struct resistive_touchscreen_4wire_calibration *cal = cfg->calibration;

  k_mutex_lock(&data->cal_lock, K_FOREVER);
  memcpy(out_cal, cal, sizeof(struct resistive_touchscreen_4wire_calibration));
  k_mutex_unlock(&data->cal_lock);

  return 0;
}

void resistive_touchscreen_4wire_set_raw_data_cb(const struct device *dev,
                                                 resistive_touchscreen_4wire_raw_data_t cb)
{
  struct touchscreen_data *data = dev->data;

  k_mutex_lock(&data->cal_lock, K_FOREVER);
  data->raw_data_cb = cb;
  k_mutex_unlock(&data->cal_lock);
}

int resistive_touchscreen_4wire_calibration_set(const struct device *dev,
                                                struct resistive_touchscreen_4wire_calibration *new_cal)
{
  const struct touchscreen_config *cfg = dev->config;
  struct touchscreen_data *data = dev->data;
  struct resistive_touchscreen_4wire_calibration *cal = cfg->calibration;

  k_mutex_lock(&data->cal_lock, K_FOREVER);
  memcpy(cal, new_cal, sizeof(struct resistive_touchscreen_4wire_calibration));
  k_mutex_unlock(&data->cal_lock);

  return 0;
}

static void insert_sort(int16_t *array, uint8_t size)
{
  uint8_t j;
  int16_t save;

  for (uint8_t i = 1; i < size; i++) {
    save = array[i];
    for (j = i; j >= 1 && save < array[j - 1]; j--) {
      array[j] = array[j - 1];
    }
    array[j] = save;
  }
}

static int adc_read_one_channel(const struct device *dev,
                                const struct adc_channel_config *channel_cfg,
                                int32_t *out)
{
  uint16_t bufs[NUMSAMPLES];
  struct adc_sequence sequence = {
    .buffer = bufs,
    .buffer_size = sizeof(bufs),
  };
  const struct adc_sequence_options options = {
    .interval_us = 0,
    .callback = NULL,
    .user_data = NULL,
    .extra_samplings = NUMSAMPLES - 1
  };
  int err;

  if (!adc_is_ready_dt(&channel_cfg->adc)) {
    LOG_ERR("ADC controller device not ready");
    return -1;
  }

  err = adc_sequence_init_dt(&channel_cfg->adc, &sequence);
  if (err < 0) {
    LOG_ERR("ADC controller device init failed (%d)", err);
    return -1;
  }

  sequence.options = &options;
  // sequence.channels |= BIT(channel_cfg->adc.channel_id);
  err = adc_read_dt(&channel_cfg->adc, &sequence);
  if (err < 0) {
    LOG_ERR("Could not read (%d)", err);
    return -1;
  }
  insert_sort(bufs, NUMSAMPLES);
  *out = (int32_t)bufs[NUMSAMPLES / 2];
  return 0;
}

static void resistive_touchscreen_4wire_loop(const struct device *dev,
                                             struct touchscreen_adc_channels *tac)
{
  const struct touchscreen_config *cfg = dev->config;
  struct touchscreen_data *data = dev->data;
  int err;
  int32_t raw_x, raw_y, raw_z1, raw_z2;
  int32_t x, y;
  bool pressed = false;
  float r_touch;

  if (!gpio_is_ready_dt(&cfg->xm_pin)
      || !gpio_is_ready_dt(&cfg->xp_pin)
      || !gpio_is_ready_dt(&cfg->ym_pin)
      || !gpio_is_ready_dt(&cfg->yp_pin)) {
    LOG_ERR("gpio not ready");
    return;
  }

  // XM = 0, XP = 1, YM = Hi-z & YP as input
  gpio_pin_configure_dt(&cfg->xm_pin, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->xp_pin, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->ym_pin, GPIO_INPUT);
  gpio_pin_configure_dt(&cfg->yp_pin, GPIO_INPUT);
  // k_usleep(20);
  err = adc_read_one_channel(dev, tac->yp_cfg, &raw_x);
  if ((err < 0) || (raw_x > 4095)) {
    LOG_ERR("Could not read x-axis (%d)", err);
    return;
  }
  raw_x = 4096 - raw_x;

  // YM = 0, YP = 1, XP = Hi-z & XM as input
  gpio_pin_configure_dt(&cfg->xm_pin, GPIO_INPUT);
  gpio_pin_configure_dt(&cfg->xp_pin, GPIO_INPUT);
  gpio_pin_configure_dt(&cfg->ym_pin, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->yp_pin, GPIO_OUTPUT_ACTIVE);
  // k_usleep(20);
  err = adc_read_one_channel(dev, tac->xm_cfg, &raw_y);
  if ((err < 0) || (raw_y > 4095)) {
    LOG_ERR("Could not read y-axis (%d)", err);
    return;
  }
  raw_y = 4096 - raw_y;

  // XP = 0, YM = 1, XM = Hi-z & YP as input
  gpio_pin_configure_dt(&cfg->xm_pin, GPIO_INPUT);
  gpio_pin_configure_dt(&cfg->xp_pin, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure_dt(&cfg->ym_pin, GPIO_OUTPUT_ACTIVE);
  gpio_pin_configure_dt(&cfg->yp_pin, GPIO_INPUT);
  // k_usleep(20);
  err = adc_read_one_channel(dev, tac->xm_cfg, &raw_z1);
  if ((err < 0) || (raw_z1 > 4095)) {
    LOG_ERR("Could not read z1-axis (%d)", err);
    return;
  }
  err = adc_read_one_channel(dev, tac->yp_cfg, &raw_z2);
  if ((err < 0) || (raw_z2 > 4095)) {
    LOG_ERR("Could not read z2-axis (%d)", err);
    return;
  }
  // LOG_INF("ADC X: %d, Y: %d, Z1 %d, Z2: %d",
  //         raw_x,
  //                            raw_y,
  //                            raw_z1,
  //                            raw_z2);

  r_touch = raw_z2;
  r_touch /= raw_z1;
  r_touch -= 1;
  r_touch *= raw_x;
  r_touch *= cfg->calibration->r_xplate;
  r_touch /= 4096;
  // LOG_INF("R plate: %d, R touch = %f", cfg->r_xplate, r_touch);

  if (data->raw_data_cb != NULL) {
    data->raw_data_cb(dev, raw_x, raw_y, r_touch);
  }

  if (cfg->xy_swap) {
    int32_t temp = raw_x;

    raw_x = raw_y;
    raw_y = temp;
  }

  if (r_touch < cfg->calibration->r_touch_thres) {
    pressed = true;

    k_mutex_lock(&data->cal_lock, K_FOREVER);

    if (raw_x > cfg->calibration->x_min) {
      raw_x -= cfg->calibration->x_min;
    } else {
      raw_x = 0;
    }

    if (raw_y > cfg->calibration->y_min) {
      raw_y -= cfg->calibration->y_min;
    } else {
      raw_y = 0;
    }

    x =
      CLAMP(raw_x * cfg->h_res
            / (cfg->calibration->x_max - cfg->calibration->x_min),
            0, cfg->h_res);
    y =
      CLAMP(raw_y * cfg->v_res
            / (cfg->calibration->y_max - cfg->calibration->y_min),
            0, cfg->v_res);

    // LOG_INF("X: %d, Y: %d",
    //                          x,
    //                          y);

    // LOG_INF("ADC X: %d, Y: %d, Z1 %d, Z2: %d",
    //          raw_x,
    //          raw_y,
    //          raw_z1,
    //          raw_z2);
    k_mutex_unlock(&data->cal_lock);
  }

  if (pressed) {
    input_report_abs(dev, INPUT_ABS_X, x, false, K_FOREVER);
    input_report_abs(dev, INPUT_ABS_Y, y, false, K_FOREVER);
    input_report_key(dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);
  } else if (data->pressed_old && !pressed) {
    input_report_key(dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);
  }
  data->pressed_old = pressed;
}

static void resistive_touchscreen_4wire_thread(void *arg1,
                                               void *arg2,
                                               void *arg3)
{
  const struct device *dev = arg1;
  const struct touchscreen_config *cfg = dev->config;
  struct touchscreen_data *data = dev->data;
  struct touchscreen_adc_channels tac = { NULL, NULL };
  int err;
  int i;

  for (i = 0; i < cfg->num_channels; i++) {
    const struct adc_channel_config *axis_cfg =
      &cfg->channel_cfg[i];

    LOG_DBG("- %s, channel %d",
            axis_cfg->adc.dev->name,
            axis_cfg->adc.channel_id);

    if (!adc_is_ready_dt(&axis_cfg->adc)) {
      LOG_ERR("ADC controller device not ready");
      return;
    }

    err = adc_channel_setup_dt(&axis_cfg->adc);
    if (err < 0) {
      LOG_ERR("Could not setup channel #%d (%d)", i, err);
      return;
    }

    if (axis_cfg->wire == INPUT_4WIRE_TS_XM) {
      tac.xm_cfg = axis_cfg;
    } else if (axis_cfg->wire == INPUT_4WIRE_TS_YP) {
      tac.yp_cfg = axis_cfg;
    }
  }

  __ASSERT(tac.xm_cfg
           && tac.xp_cfg
           && tac.ym_cfg
           && tac.yp_cfg,
           "Invalid config");

  __ASSERT(cfg->xm_pin.port
           && cfg->xp_pin.port
           && cfg->ym_pin.port
           && cfg->yp_pin.port,
           "No gpio supplied");

  k_timer_init(&data->timer, NULL, NULL);
  k_timer_start(&data->timer,
                K_MSEC(cfg->poll_period_ms), K_MSEC(cfg->poll_period_ms));

  while (true) {
    resistive_touchscreen_4wire_loop(dev, &tac);
    k_timer_status_sync(&data->timer);
  }
}

static int resistive_touchscreen_4wire_init(const struct device *dev)
{
  struct touchscreen_data *data = dev->data;
  k_tid_t tid;
  k_mutex_init(&data->cal_lock);
  data->pressed_old = false;

  tid = k_thread_create(&data->thread,
                        data->thread_stack,
                        K_KERNEL_STACK_SIZEOF(
                          data->thread_stack),
                        resistive_touchscreen_4wire_thread,
                        (void *)dev,
                        NULL,
                        NULL,
                        CONFIG_INPUT_RESISTIVE_TOUCHSCREEN_4WIRE_THREAD_PRIORITY,
                        0,
                        K_NO_WAIT);
  if (!tid) {
    LOG_ERR("thread creation failed");
    return -ENODEV;
  }

  k_thread_name_set(&data->thread, dev->name);

  return 0;
}

#define RESISTIVE_TOUCH_CHANNEL_CFG_DEF(node_id) \
  {                                              \
    .adc = ADC_DT_SPEC_GET(node_id),             \
    .wire = DT_PROP(node_id, zephyr_wire),       \
  }

#define RESISTIVE_TOUCH_INIT(inst)                                                             \
  static const struct adc_channel_config                                                       \
  resistive_touchscreen_4wire_channel_cfg_ ## inst[] = {                                       \
    DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(inst,                                                \
                                          RESISTIVE_TOUCH_CHANNEL_CFG_DEF,                     \
                                          (, ))                                                \
  };                                                                                           \
                                                                                               \
  static struct adc_channel_data                                                               \
  resistive_touchscreen_4wire_channel_data_ ## inst[ARRAY_SIZE(                                \
                                                      resistive_touchscreen_4wire_channel_cfg_ \
                                                      ## inst)];                               \
                                                                                               \
  static struct resistive_touchscreen_4wire_calibration                                        \
  resistive_touchscreen_4wire_calibration ## inst = {                                          \
    .x_min = DT_INST_PROP(inst, x_min),                                                        \
    .x_max = DT_INST_PROP(inst, x_max),                                                        \
    .y_min = DT_INST_PROP(inst, y_min),                                                        \
    .y_max = DT_INST_PROP(inst, y_max),                                                        \
    .r_xplate = DT_INST_PROP(inst, r_xplate),                                                  \
    .r_touch_thres = DT_INST_STRING_UNQUOTED(inst, r_touch_threshold),                         \
  };                                                                                           \
                                                                                               \
  static const struct touchscreen_config                                                       \
                      resistive_touchscreen_4wire_cfg_ ## inst = {                             \
    .poll_period_ms = DT_INST_PROP(inst, poll_period_ms),                                      \
    .h_res = DT_INST_PROP(inst, h_res),                                                        \
    .v_res = DT_INST_PROP(inst, v_res),                                                        \
    .xy_swap = DT_INST_PROP_OR(inst, xy_swap, false),                                          \
    .xm_pin = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), xm_gpios),                                   \
    .xp_pin = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), xp_gpios),                                   \
    .ym_pin = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), ym_gpios),                                   \
    .yp_pin = GPIO_DT_SPEC_GET(DT_DRV_INST(inst), yp_gpios),                                   \
    .channel_cfg = resistive_touchscreen_4wire_channel_cfg_ ## inst,                           \
    .channel_data = resistive_touchscreen_4wire_channel_data_ ## inst,                         \
    .calibration = &resistive_touchscreen_4wire_calibration ## inst,                           \
    .num_channels = ARRAY_SIZE(                                                                \
      resistive_touchscreen_4wire_channel_cfg_ ## inst),                                       \
  };                                                                                           \
                                                                                               \
  static struct touchscreen_data                                                               \
  resistive_touchscreen_4wire_data_ ## inst;                                                   \
                                                                                               \
  DEVICE_DT_INST_DEFINE(inst,                                                                  \
                        resistive_touchscreen_4wire_init,                                      \
                        NULL,                                                                  \
                        &resistive_touchscreen_4wire_data_ ## inst,                            \
                        &resistive_touchscreen_4wire_cfg_ ## inst,                             \
                        POST_KERNEL,                                                           \
                        CONFIG_INPUT_INIT_PRIORITY,                                            \
                        NULL);

DT_INST_FOREACH_STATUS_OKAY(RESISTIVE_TOUCH_INIT)
