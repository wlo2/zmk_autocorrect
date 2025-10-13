/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_autocorrect_toggle

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>

#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zephyr/kernel.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

static inline void hid_clear_and_flush(void) {
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
}

static void tap_code(uint16_t keycode) {
    zmk_hid_keyboard_press(keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
    zmk_hid_keyboard_release(keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
}

static void send_string_lower(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') {
            uint16_t keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
            tap_code(keycode);
        } else if (c == ' ') {
            tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR));
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
    static uint32_t last_ms = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_ms < 150) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    last_ms = now;

    autocorrect_toggle();

#if CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK
    // Type lowercase feedback and clear HID to avoid stuck mods
    if (autocorrect_is_enabled()) {
        send_string_lower("on");
    } else {
        send_string_lower("off");
    }
    hid_clear_and_flush();
#endif
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
