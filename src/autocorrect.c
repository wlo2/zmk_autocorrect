#include <zmk/endpoints.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/nvs.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <string.h>
#include <zmk/keys.h>
#include <zephyr/storage/flash_map.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define AUTOCORRECT_DEBUG 0

#if __has_include("autocorrect_data.h")
#    include "autocorrect_data.h"
#else
#    pragma message "Autocorrect is using the default library."
#    include "autocorrect_data_default.h"
#endif

#define AUTOCORRECT_ENABLE_ID 1

#if FIXED_PARTITION_EXISTS(storage)
static struct nvs_fs fs;
#endif
static uint8_t typo_buffer[AUTOCORRECT_MAX_LENGTH] = {HID_USAGE_KEY_KEYBOARD_SPACEBAR}; // Initialize with space
static uint8_t typo_buffer_size = 1;

// Correction work container struct
struct autocorrect_correction_work {
    struct k_work_delayable work;
    uint8_t backspaces;
    char replacement[AUTOCORRECT_MAX_LENGTH + 10];
    bool active;
};
static struct autocorrect_correction_work correction_work;

// Correction work handler
static void correction_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct autocorrect_correction_work *cw = CONTAINER_OF(dwork, struct autocorrect_correction_work, work);
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Executing correction");
#endif
    cw->active = false;
    // Send backspaces
    for (uint8_t i = 0; i < cw->backspaces; ++i) {
        zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE));
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
        zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE));
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
    }
    // Send replacement string
    for (int i = 0; cw->replacement[i] != '\0'; i++) {
        char c = cw->replacement[i];
        uint16_t keycode;
        if (c >= 'a' && c <= 'z') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
            zmk_hid_keyboard_press(keycode);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
            zmk_hid_keyboard_release(keycode);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
        } else if (c >= 'A' && c <= 'Z') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
            zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
            zmk_hid_keyboard_press(keycode);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
            zmk_hid_keyboard_release(keycode);
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
            zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
        }
        // Skip non-alphabetic for now
    }
}

// Initialize NVS
static int autocorrect_init(void) {
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Initializing...");
#endif
#if FIXED_PARTITION_EXISTS(storage)
    int rc;
    fs.offset = FLASH_AREA_OFFSET(storage);
    fs.sector_size = 4096;
    fs.sector_count = 3;
    rc = nvs_mount(&fs);
    if (rc) {
#if AUTOCORRECT_DEBUG
        LOG_ERR("Autocorrect: NVS mount failed: %d", rc);
#endif
        return rc;
    }
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: NVS mounted successfully");
#endif
#endif
    k_work_init_delayable(&correction_work.work, correction_work_handler);
    correction_work.active = false;
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Initialized successfully, enabled=%s", autocorrect_is_enabled() ? "true" : "false");
#endif
    return 0;
}

SYS_INIT(autocorrect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static bool autocorrect_enabled = true; // Default state

bool autocorrect_is_enabled(void) {
#if FIXED_PARTITION_EXISTS(storage)
    uint8_t enabled;
    int rc = nvs_read(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
    if (rc > 0) { // item was found
        autocorrect_enabled = (enabled == 1);
        return autocorrect_enabled;
    }
#endif
    return autocorrect_enabled; // return cached state or default
}

void autocorrect_enable(void) {
    autocorrect_enabled = true;
#if FIXED_PARTITION_EXISTS(storage)
    uint8_t enabled = 1;
    (void)nvs_write(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
#endif
}

void autocorrect_disable(void) {
    autocorrect_enabled = false;
#if FIXED_PARTITION_EXISTS(storage)
    uint8_t enabled = 0;
    (void)nvs_write(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
#endif
}

void autocorrect_toggle(void) {
    if (autocorrect_is_enabled()) {
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Disabling");
#endif
        autocorrect_disable();
    } else {
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Enabling");
#endif
        autocorrect_enable();
    }
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Toggle completed, now %s", autocorrect_enabled ? "enabled" : "disabled");
#endif
}

static void tap_code(uint16_t keycode) {
    zmk_hid_keyboard_press(keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
    zmk_hid_keyboard_release(keycode);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
}

static void send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        uint16_t keycode;
        if (c >= 'a' && c <= 'z') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
            tap_code(keycode);
        } else if (c >= 'A' && c <= 'Z') {
            keycode = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
            zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
            tap_code(keycode);
            zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
            zmk_endpoints_send_report(HID_USAGE_KEY);
            k_sleep(K_MSEC(CONFIG_ZMK_AUTOCORRECT_DELAY_MS));
        }
        // Skip non-alphabetic for now
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

#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Key event - keycode=0x%04X, enabled=%s", ev->keycode, autocorrect_is_enabled() ? "true" : "false");
#endif

    if (!autocorrect_is_enabled()) {
        typo_buffer_size = 0;
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!process_autocorrect_user(ev)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Require keyboard usage page (0x07)
    if (ev->usage_page != HID_USAGE_KEY) {
        typo_buffer_size = 1;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        return ZMK_EV_EVENT_BUBBLE;
    }
    uint8_t usage8 = (uint8_t)ev->keycode;
    if (usage8 == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        if (typo_buffer_size > 0) { --typo_buffer_size; }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (usage8 == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) {
        typo_buffer_size = 1;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        return ZMK_EV_EVENT_BUBBLE;
    }
    // Append to buffer (apply MAX_LENGTH sliding window as you already do)
    if (typo_buffer_size >= AUTOCORRECT_MAX_LENGTH) {
        memmove(typo_buffer, typo_buffer + 1, AUTOCORRECT_MAX_LENGTH - 1);
        typo_buffer_size = AUTOCORRECT_MAX_LENGTH - 1;
    }
    typo_buffer[typo_buffer_size++] = usage8;
    if (typo_buffer_size < AUTOCORRECT_MIN_LENGTH) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint16_t state = 0;
    uint8_t code = autocorrect_data[state];
    for (int8_t i = typo_buffer_size - 1; i >= 0; --i) {
        uint8_t const key_i = typo_buffer[i];
        if (code & 64) {
            code &= 63;
            for (; code != key_i; code = autocorrect_data[state += 3]) {
                if (!code)
                    return ZMK_EV_EVENT_BUBBLE;
            }
            state = (autocorrect_data[state + 1] | autocorrect_data[state + 2] << 8);
        } else if (code != key_i) {
            return ZMK_EV_EVENT_BUBBLE;
        } else if (!(code = autocorrect_data[++state])) {
            ++state;
        }
        if (state >= DICTIONARY_SIZE) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        code = autocorrect_data[state];
        if (code & 128) {
            const uint8_t backspaces = (code & 63);
            const char *changes = (const char *)(autocorrect_data + state + 1);

#if AUTOCORRECT_DEBUG
            LOG_INF("Autocorrect: TYPO FOUND! backspaces=%d, changes='%s'", backspaces, changes);
#endif

            char typo[AUTOCORRECT_MAX_LENGTH + 1] = {0};
            uint8_t typo_len = 0;
            uint8_t typo_start = 0;
            bool space_last = typo_buffer[typo_buffer_size - 1] == HID_USAGE_KEY_KEYBOARD_SPACEBAR;
            for (uint8_t i = typo_buffer_size; i > 0; --i) {
                if (typo_buffer[i - 1] == HID_USAGE_KEY_KEYBOARD_SPACEBAR && i != typo_buffer_size) {
                    typo_start = i;
                    break;
                }
                ++typo_len;
            }
            if (space_last) {
                --typo_len;
            }
            uint8_t j = 0;
            for (uint8_t i = 0; i < typo_len; ++i) {
                uint8_t b = typo_buffer[typo_start + i];
                if (b >= HID_USAGE_KEY_KEYBOARD_A && b <= HID_USAGE_KEY_KEYBOARD_Z) {
                    typo[j++] = 'a' + (b - HID_USAGE_KEY_KEYBOARD_A);
                }
            }
            typo[j] = '\0';
            char correct[AUTOCORRECT_MAX_LENGTH + 10] = {0};
            size_t start = (backspaces > typo_len) ? 0 : (typo_len - backspaces);
            size_t maxlen = sizeof(correct) - 1;
            if (start > 0 && start < maxlen) {
                memcpy(correct, typo, start);
            }
            correct[start] = '\0';
            strncat(correct, changes, maxlen - strlen(correct));
            if (space_last && strlen(correct) < maxlen) {
                strncat(correct, " ", maxlen - strlen(correct));
            }

            if (apply_autocorrect(backspaces, changes, typo, correct)) {
                if (correction_work.active) {
#if AUTOCORRECT_DEBUG
                    LOG_INF("Autocorrect: Correction already active, skipping");
#endif
                    return ZMK_EV_EVENT_BUBBLE;
                }
#if AUTOCORRECT_DEBUG
                LOG_INF("Autocorrect: Queuing correction work");
#endif
                correction_work.active = true;
                correction_work.backspaces = backspaces;
                strncpy(correction_work.replacement, changes, sizeof(correction_work.replacement) - 1);
                correction_work.replacement[sizeof(correction_work.replacement) - 1] = '\0';
                k_work_schedule(&correction_work.work, K_MSEC(CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS));
            }

            // When resetting buffer after correction
            if (usage8 == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
                typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
                typo_buffer_size = 1;
                return ZMK_EV_EVENT_BUBBLE;
            } else {
                typo_buffer_size = 0;
                return ZMK_EV_EVENT_BUBBLE;
            }
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(autocorrect_listener, autocorrect_event_listener);
ZMK_SUBSCRIPTION(autocorrect_listener, zmk_keycode_state_changed);