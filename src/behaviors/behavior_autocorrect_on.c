/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect_on

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zmk/autocorrect.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_autocorrect_on_init(const struct device *dev) {
    return 0;
}

static int behavior_autocorrect_on_action(const struct device *dev) {
    autocorrect_enable();
    return 0;
}

DEVICE_DT_INST_DEFINE(0, behavior_autocorrect_on_init, NULL, NULL, NULL, 
                      POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, NULL);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
