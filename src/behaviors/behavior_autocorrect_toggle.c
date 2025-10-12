/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

// Function to send a single key press/release
static void send_key_event(uint32_t usage, bool pressed) {
    struct zmk_keycode_state_changed *ev = new_zmk_keycode_state_changed();
    ev->usage = usage;
    ev->state = pressed;
    ev->timestamp = k_uptime_get();
    ZMK_EVENT_RAISE(ev);
}

// Visual feedback function
static void tap_key(uint32_t usage) {
    send_key_event(usage, true);   // Press
    k_msleep(50);
    send_key_event(usage, false);  // Release
    k_msleep(50);
}
#endif


#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_autocorrect_toggle_init(const struct device *dev) {
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    LOG_INF("Autocorrect toggle behavior pressed");
    
#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    LOG_INF("Autocorrect toggle: Running on central/single board");
    autocorrect_toggle();
    
    // Visual feedback: Send a quick indicator when toggling
    // This helps confirm the toggle is working without console
    if (autocorrect_is_enabled()) {
        LOG_INF("Autocorrect is now enabled, sending 'ON'");
        // Send "ON" to show it's enabled
        tap_key(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_O));
        tap_key(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_N));
    } else {
        LOG_INF("Autocorrect is now disabled, sending 'OFF'");
        // Send "OFF" to show it's disabled  
        tap_key(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_O));
        tap_key(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F));
        tap_key(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F));
    }
#else
    LOG_INF("Autocorrect toggle: Running on peripheral (no-op)");
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_autocorrect_toggle_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_autocorrect_toggle_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_autocorrect_toggle_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
