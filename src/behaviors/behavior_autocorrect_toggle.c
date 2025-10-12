/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zmk/usb_hid.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

// Forward declare tap_code function
static void tap_code(uint16_t keycode) {
    zmk_hid_keyboard_press(keycode);
    zmk_usb_hid_send_keyboard_report();
    zmk_hid_keyboard_release(keycode);
    zmk_usb_hid_send_keyboard_report();
}
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_autocorrect_toggle_init(const struct device *dev) {
    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    autocorrect_toggle();
    
    // Visual feedback: Send a quick "AC" when toggling
    // This helps confirm the toggle is working without console
    if (autocorrect_is_enabled()) {
        // Send "ON" to show it's enabled
        tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_O));
        tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_N));
    } else {
        // Send "OFF" to show it's disabled  
        tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_O));
        tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F));
        tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_F));
    }
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
