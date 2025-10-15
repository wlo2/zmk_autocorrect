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
#include <zmk/keys.h>

#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
#include <zmk/autocorrect.h>
#include <zmk/autocorrect_internal.h>
#include <zmk/hid.h>
#include <zephyr/kernel.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

#if CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK
static void send_string_lower(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c >= 'a' && c <= 'z') {
            zmk_key_t keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
            press_and_release(keycode);
        } else if (c == ' ') {
            press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR));
        }
    }
}
#endif
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_autocorrect_toggle_init(const struct device *dev) {
    return 0;
}

static int on_autocorrect_toggle_binding_pressed(struct zmk_behavior_binding *binding,
                                                struct zmk_behavior_binding_event event) {
#if (!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    static uint32_t last_press_ms = 0;
    static uint8_t tap_count = 0;
    uint32_t now = k_uptime_get_32();
    
    // Multi-tap detection: 500ms window for diagnostic modes
    if (now - last_press_ms < 500) {
        tap_count++;
    } else {
        tap_count = 1;
    }
    
    // Debounce: prevent accidental double-triggers
    if (now - last_press_ms < 150 && tap_count == 1) {
        return ZMK_BEHAVIOR_OPAQUE;
    }
    
    last_press_ms = now;
    
    // Single press: toggle on/off
    if (tap_count == 1) {
        autocorrect_toggle();
        
#if CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS
        // Type minimal feedback: "1" (on) or "0" (off)
        char feedback = autocorrect_is_enabled() ? '1' : '0';
        zmk_key_t keycode = ZMK_HID_USAGE(HID_USAGE_KEY, 
            (feedback == '1' ? HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION : HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS));
        autocorrect_set_suppress(true);
        press_and_release(keycode);
        hid_clear_and_flush();
        autocorrect_set_suppress(false);
#endif
    }
#if CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS
    // Double press: type buffer size
    else if (tap_count == 2) {
        uint8_t buf_size = autocorrect_get_buffer_size();
        char feedback = (buf_size <= 9) ? ('0' + buf_size) : 'X';
        
        zmk_key_t keycode;
        if (feedback == 'X') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_X);
        } else if (feedback == '0') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
        } else {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (feedback - '1'));
        }
        autocorrect_set_suppress(true);
        press_and_release(keycode);
        hid_clear_and_flush();
        autocorrect_set_suppress(false);
    }
    // Triple press: type dictionary validity
    else if (tap_count == 3) {
        bool valid = autocorrect_dict_is_valid();
        char feedback = valid ? 'y' : 'n';
        
        zmk_key_t keycode = ZMK_HID_USAGE(HID_USAGE_KEY, 
            (feedback == 'y' ? HID_USAGE_KEY_KEYBOARD_Y : HID_USAGE_KEY_KEYBOARD_N));
        autocorrect_set_suppress(true);
        press_and_release(keycode);
        hid_clear_and_flush();
        autocorrect_set_suppress(false);
    }
    // Quadruple press: type lookup count (modulo 10)
    else if (tap_count >= 4) {
        uint32_t lookup_count = autocorrect_get_lookup_count();
        char feedback = '0' + (lookup_count % 10);
        
        zmk_key_t keycode;
        if (feedback == '0') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS);
        } else {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + (feedback - '1'));
        }
        autocorrect_set_suppress(true);
        press_and_release(keycode);
        hid_clear_and_flush();
        autocorrect_set_suppress(false);
        tap_count = 0; // Reset after diagnostic
    }
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
