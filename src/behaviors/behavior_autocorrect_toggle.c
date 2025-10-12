/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zmk/usb_hid.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

static void tap_code(uint16_t keycode) {
    zmk_hid_keyboard_press(keycode);
    zmk_usb_hid_send_keyboard_report();
    zmk_hid_keyboard_release(keycode);
    zmk_usb_hid_send_keyboard_report();
}

static void send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        uint16_t keycode;
        
        if (c >= 'A' && c <= 'Z') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
            tap_code(keycode);
        }
    }
}
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_autocorrect_toggle_init(const struct device *dev) {
    return 0;
}

static int on_autocorrect_toggle_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event event) {
#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    autocorrect_toggle();
    
    // Visual feedback
    if (autocorrect_is_enabled()) {
        send_string("ON");
    } else {
        send_string("OFF");
    }
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_autocorrect_toggle_binding_released(struct zmk_behavior_binding *binding,
                                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_autocorrect_toggle_driver_api = {
    .binding_pressed = on_autocorrect_toggle_binding_pressed,
    .binding_released = on_autocorrect_toggle_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_autocorrect_toggle_init, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_autocorrect_toggle_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
