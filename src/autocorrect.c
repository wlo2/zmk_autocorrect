#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/nvs.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zmk/usb_hid.h>
#include <zmk/keys.h>
#include <zephyr/storage/flash_map.h>
#include <dt-bindings/zmk/modifiers.h>

#if __has_include("autocorrect_data.h")
#    include "autocorrect_data.h"
#else
#    pragma message "Autocorrect is using the default library."
#    include "autocorrect_data_default.h"
#endif

#define AUTOCORRECT_ENABLE_ID 1

static struct nvs_fs fs;
static uint8_t typo_buffer[AUTOCORRECT_MAX_LENGTH] = {0};
static uint8_t typo_buffer_size = 0;

// Initialize NVS
static int autocorrect_init(void) {
        int rc;
#if FIXED_PARTITION_EXISTS(storage)
    fs.offset = FLASH_AREA_OFFSET(storage);
    rc = nvs_init(&fs, FLASH_AREA_ID(storage));
#else
    fs.offset = 0;
    rc = -1;
#endif
    if (rc) {
        return rc;
    }
    return 0;
}

SYS_INIT(autocorrect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

bool autocorrect_is_enabled(void) {
    uint8_t enabled;
    int rc = nvs_read(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
    if (rc > 0) { // item was found
        return enabled == 1;
    }
    return true; // default to enabled
}

void autocorrect_enable(void) {
    uint8_t enabled = 1;
    (void)nvs_write(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
}

void autocorrect_disable(void) {
    uint8_t enabled = 0;
    (void)nvs_write(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
}

void autocorrect_toggle(void) {
    if (autocorrect_is_enabled()) {
        autocorrect_disable();
    } else {
        autocorrect_enable();
    }
}

static void tap_code(uint16_t keycode) {
    zmk_hid_keyboard_press(keycode);
    zmk_usb_hid_send_keyboard_report();
    zmk_hid_keyboard_release(keycode);
    zmk_usb_hid_send_keyboard_report();
}

static void send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        // This is a simplification. A real implementation would need to handle
        // shifted characters and other symbols.
        tap_code(str[i]);
    }
}

__attribute__((weak))
bool apply_autocorrect(uint8_t backspaces, const char *str, char *typo, char *correct) {
    return true;
}

__attribute__((weak))
bool process_autocorrect_user(struct zmk_keycode_state_changed *ev) {
    return true;
}

static int autocorrect_event_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!autocorrect_is_enabled()) {
        typo_buffer_size = 0;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!process_autocorrect_user(ev)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint16_t keycode = ev->keycode;

    // Disable autocorrect while a mod other than shift is active.
    if ((zmk_hid_get_explicit_mods() & ~(MOD_LSFT | MOD_RSFT)) != 0) {
        typo_buffer_size = 0;
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (keycode) {
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_A) ... ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_Z):
        // process normally
        break;
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_1) ... ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_0):
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_TAB) ... ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SEMICOLON):
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE) ... ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK):
        // Set a word boundary if space, period, digit, etc. is pressed.
        keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
        break;
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_ENTER):
        // Behave more conservatively for the enter key. Reset, so that enter
        // can't be used on a word ending.
        typo_buffer_size = 0;
        keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
        break;
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_BACKSPACE):
        // Remove last character from the buffer.
        if (typo_buffer_size > 0) {
            --typo_buffer_size;
        }
        return ZMK_EV_EVENT_BUBBLE;
    case ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE):
        // Treat " (shifted ') as a word boundary.
        if ((zmk_hid_get_explicit_mods() & MOD_MASK_SHIFT) != 0) {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
        }
        break;
    default:
        // Clear state if some other non-alpha key is pressed.
        typo_buffer_size = 0;
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Rotate oldest character if buffer is full.
    if (typo_buffer_size >= AUTOCORRECT_MAX_LENGTH) {
        memmove(typo_buffer, typo_buffer + 1, AUTOCORRECT_MAX_LENGTH - 1);
        typo_buffer_size = AUTOCORRECT_MAX_LENGTH - 1;
    }

    // Append `keycode` to buffer.
    typo_buffer[typo_buffer_size++] = keycode;
    // Return if buffer is smaller than the shortest word.
    if (typo_buffer_size < AUTOCORRECT_MIN_LENGTH) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Check for typo in buffer using a trie stored in `autocorrect_data`.
    uint16_t state = 0;
    uint8_t code = autocorrect_data[state];
    for (int8_t i = typo_buffer_size - 1; i >= 0; --i) {
        uint8_t const key_i = typo_buffer[i];

        if (code & 64) { // Check for match in node with multiple children.
            code &= 63;
            for (; code != key_i; code = autocorrect_data[state += 3]) {
                if (!code)
                    return ZMK_EV_EVENT_BUBBLE;
            }
            // Follow link to child node.
            state = (autocorrect_data[state + 1] | autocorrect_data[state + 2] << 8);
            // Check for match in node with single child.
        } else if (code != key_i) {
            return ZMK_EV_EVENT_BUBBLE;
        } else if (!(code = autocorrect_data[++state])) {
            ++state;
        }

        // Stop if `state` becomes an invalid index. This should not normally
        // happen, it is a safeguard in case of a bug, data corruption, etc.
        if (state >= DICTIONARY_SIZE) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        code = autocorrect_data[state];

        if (code & 128) { // A typo was found! Apply autocorrect.
            const uint8_t backspaces = (code & 63) + ev->state;
            const char *changes = (const char *)(autocorrect_data + state + 1);

            char typo[AUTOCORRECT_MAX_LENGTH + 1] = {0};
            uint8_t typo_len = 0;
            uint8_t typo_start = 0;
            bool space_last = typo_buffer[typo_buffer_size - 1] == ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
            for (uint8_t i = typo_buffer_size; i > 0; --i) {
                if (typo_buffer[i - 1] == ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR) && i != typo_buffer_size) {
                    typo_start = i;
                    break;
                }
                ++typo_len;
            }

            if (space_last) {
                --typo_len;
            }

            for (uint8_t i = 0; i < typo_len; ++i) {
                typo[i] = typo_buffer[typo_start + i] - ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_A) + 'a';
            }

            char correct[AUTOCORRECT_MAX_LENGTH + 10] = {0};
            uint8_t offset = space_last ? backspaces : backspaces + 1;
            strcpy(correct, typo);
            strcpy(correct + typo_len - offset, changes);

            if (apply_autocorrect(backspaces, changes, typo, correct)) {
                for (uint8_t i = 0; i < backspaces; ++i) {
                    tap_code(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_BACKSPACE));
                }
                send_string(changes);
            }

            if (keycode == ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR)) {
                typo_buffer[0] = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
                typo_buffer_size = 1;
                return ZMK_EV_EVENT_BUBBLE;
            } else {
                typo_buffer_size = 0;
                return ZMK_EV_EVENT_HANDLED;
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(autocorrect_listener, autocorrect_event_listener);
ZMK_SUBSCRIPTION(autocorrect_listener, zmk_keycode_state_changed);