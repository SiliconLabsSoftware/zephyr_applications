/*
 * Copyright (c) 2025 Silicon Laboratories Inc. www.silabs.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/input/input_resistive_touchscreen_4wire.h>
#include <zephyr/input/input_resistive_touchscreen_4wire_settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(resistive_touchscreen_4wire_settings,
                    CONFIG_INPUT_LOG_LEVEL);

#define RESISTIVE_TOUCH_SETTINGS_PATH_MAX 32

static void resistive_touchscreen_4wire_calibration_log(
  const struct device *dev)
{
  struct resistive_touchscreen_4wire_calibration cal;

  resistive_touchscreen_4wire_calibration_get(dev, &cal);

  LOG_INF(
    "%s: x-min: %d x-max: %d y-min: %d y-max: %d r_xplate: %d r_touch_thres: %f",
    dev->name,
    cal.x_min,
    cal.x_max,
    cal.y_min,
    cal.y_max,
    cal.r_xplate,
    (double)cal.r_touch_thres);
}

static int resistive_touchscreen_4wire_calibration_load(const char *key,
                                                        size_t len_rd,
                                                        settings_read_cb read_cb,
                                                        void *cb_arg)
{
  const struct device *dev;
  struct resistive_touchscreen_4wire_calibration cal;
  char dev_name[RESISTIVE_TOUCH_SETTINGS_PATH_MAX];
  const char *next;
  int nlen;
  ssize_t len;

  nlen = settings_name_next(key, &next);
  if (nlen + 1 > sizeof(dev_name)) {
    LOG_ERR("Setting name too long: %d", nlen);
    return -EINVAL;
  }

  memcpy(dev_name, key, nlen);
  dev_name[nlen] = '\0';

  dev = device_get_binding(dev_name);
  if (dev == NULL) {
    LOG_ERR("Cannot restore: device %s not available", dev_name);
    return -ENODEV;
  }

  len = read_cb(cb_arg, &cal, sizeof(cal));
  if (len < 0) {
    LOG_ERR("Data restore error: %d", len);
  }

  if (len != sizeof(struct resistive_touchscreen_4wire_calibration)) {
    LOG_ERR("Invalid settings data length: %d, expected %d",
            len, sizeof(struct resistive_touchscreen_4wire_calibration));
    return -EIO;
  }

  resistive_touchscreen_4wire_calibration_set(dev, &cal);

  resistive_touchscreen_4wire_calibration_log(dev);

  return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(resistive_touchscreen_4wire,
                               "aa-cal",
                               NULL,
                               resistive_touchscreen_4wire_calibration_load,
                               NULL,
                               NULL);

int resistive_touchscreen_4wire_calibration_save(const struct device *dev)
{
  struct resistive_touchscreen_4wire_calibration cal;
  char path[RESISTIVE_TOUCH_SETTINGS_PATH_MAX];
  int ret;

  resistive_touchscreen_4wire_calibration_log(dev);

  ret = snprintk(path, sizeof(path), "aa-cal/%s", dev->name);
  if (ret < 0) {
    return -EINVAL;
  }

  resistive_touchscreen_4wire_calibration_get(dev, &cal);

  ret = settings_save_one(path, &cal,
                          sizeof(struct resistive_touchscreen_4wire_calibration));
  if (ret < 0) {
    LOG_ERR("Settings save errord: %d", ret);
    return ret;
  }

  return 0;
}
