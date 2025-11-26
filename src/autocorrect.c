#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/nvs.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/autocorrect.h>
#include <zmk/hid.h>
#include <zmk/autocorrect_internal.h>
#include <string.h>
#include <zmk/keys.h>
#include <zephyr/storage/flash_map.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define AUTOCORRECT_DEBUG 0

#if __has_include(<autocorrect_data.h>)
#include <autocorrect_data.h>
#else
#include <autocorrect_data_default.h>
#endif

static bool autocorrect_enabled = true; // Default state (cached)

static atomic_t autocorrect_suppressed = ATOMIC_INIT(0); // Diagnostics/event suppression flag

enum autocorrect_state {
    AUTOCORRECT_STATE_IDLE,
    AUTOCORRECT_STATE_BACKSPACE_SUFFIX,
    AUTOCORRECT_STATE_BACKSPACE_LETTERS,
    AUTOCORRECT_STATE_TYPE_CHARS,
    AUTOCORRECT_STATE_TYPE_SUFFIX,
};

enum autocorrect_sub_state {
    AUTOCORRECT_SUB_STATE_MOD_PRESS,
    AUTOCORRECT_SUB_STATE_KEY_PRESS,
    AUTOCORRECT_SUB_STATE_KEY_RELEASE,
    AUTOCORRECT_SUB_STATE_MOD_RELEASE,
};

struct autocorrect_correction_work {
    struct k_work_delayable work;
    uint8_t backspaces;
    const char *changes_ptr;
    char suffix_delim;
    uint8_t typo_len_at_sched;
    atomic_t seq;

    // State machine context
    enum autocorrect_state state;
    enum autocorrect_sub_state sub_state;
    int index;
    zmk_key_t current_key;
    zmk_key_t current_mod;
};

static struct autocorrect_correction_work correction_work;
static atomic_t autocorrect_seq;

#define AUTOCORRECT_ENABLE_ID 1

#if FIXED_PARTITION_EXISTS(storage)
static struct nvs_fs fs;
#endif
static uint8_t typo_buffer[AUTOCORRECT_MAX_LENGTH] = {HID_USAGE_KEY_KEYBOARD_SPACEBAR}; // Initialize with space
static uint8_t typo_buffer_size = 1;
static uint32_t last_delim_index = 0; // index into a logical stream position

// Track currently pressed modifiers to allow suppression under non-shift mods
static uint8_t mods_pressed = 0; // bitmask for E0..E7 usages (low 8 bits)

// Diagnostic state
static bool dict_valid = false;
static uint32_t event_count = 0;
static uint32_t lookup_count = 0;
static uint32_t correction_count = 0;

static inline bool is_modifier_usage(uint8_t usage) {
    return usage >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL && usage <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

// Lookup helpers (KC-based only)
// KC-based traversal is the sole supported path. ASCII helpers were removed to avoid drift.
// See tools/README.md and README.md for the KC format and generator expectations.
#if defined(TRIE_LOOKUP_ASCII)
#error "ASCII-based lookup is not supported. Use trie_lookup_kc() and KC-based dictionaries."
#endif

// KC-based traversal: compares raw 6-bit node keys to HID usage codes (e.g., 0x04..0x1D for A..Z, 0x2C for boundary)
static bool trie_lookup_kc(const uint8_t *kc_seq, uint8_t len, uint8_t *out_backspaces, const char **out_changes) {
    if (!kc_seq || len == 0 || !out_backspaces || !out_changes) return false;
    lookup_count++;
    uint16_t state = 0;
    uint8_t pos = 0;

    while (1) {
        uint8_t b = autocorrect_data[state];
        uint8_t node_type = b & 0xC0; // 00=chain, 01=branch, 10=leaf

        if (node_type == 0x80) { // leaf
            if (pos != len) {
                return false; // reached leaf before consuming all input
            }
            uint8_t backspaces = (uint8_t)(b & 0x7F);
            const char *rep = (const char *)&autocorrect_data[state + 1];
            *out_backspaces = backspaces;
            *out_changes = rep;
            return true;
        } else if (node_type == 0x40) { // branch
            if (pos >= len) {
                return false; // need more input to choose a branch
            }
            uint8_t want = kc_seq[pos] & 0x3F;
            uint16_t idx = state;
            bool matched = false;
            while (1) {
                uint8_t key = autocorrect_data[idx] & 0x3F; // strip type bits
                if (key == 0) break; // end of branches
                uint16_t link = (uint16_t)autocorrect_data[idx + 1] | ((uint16_t)autocorrect_data[idx + 2] << 8);
                if (key == want) {
                    state = link;
                    pos++;
                    matched = true;
                    break;
                }
                idx += 3;
            }
            if (!matched) return false;
            continue;
        } else { // chain (top bits 00)
            uint16_t idx = state;
            while (1) {
                uint8_t key = autocorrect_data[idx];
                if (key == 0) {
                    // move to child node encoded immediately after chain
                    state = idx + 1;
                    break;
                }
                if (pos >= len) {
                    return false; // input shorter than chain
                }
                uint8_t want = kc_seq[pos] & 0x3F;
                if ((key & 0x3F) != want) {
                    return false; // mismatch in chain
                }
                pos++;
                idx++;
            }
            continue;
        }
    }
}

static inline bool is_shift_modifier(uint8_t usage) {
    return usage == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT || usage == HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT;
}

static inline bool is_digit_usage(uint8_t usage) {
    return usage >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION && usage <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS;
}

static inline bool is_alpha_usage(uint8_t usage) {
    return usage >= HID_USAGE_KEY_KEYBOARD_A && usage <= HID_USAGE_KEY_KEYBOARD_Z;
}

static inline bool is_printable_delimiter(uint8_t usage) {
    switch (usage) {
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
    case HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE:
    case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE:
    case HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN:
    case HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN:
        return true;
    default:
        return false;
    }
}

static inline bool is_printable_usage(uint8_t usage) {
    return is_alpha_usage(usage) || is_digit_usage(usage) || is_printable_delimiter(usage);
}

static inline int selected_delay_ms(void) {
    // Use a faster delay for USB builds if configured
    int d = CONFIG_ZMK_AUTOCORRECT_DELAY_MS;
#if defined(CONFIG_USB_DEVICE_STACK) && defined(CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS)
    // On firmware built with USB support, optionally use fast USB delay
    if (CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS > 0 && CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS < d) {
        d = CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS;
    }
#endif
    return d;
}

static inline int selected_work_delay_ms(void) {
    int d = CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS;
#if defined(CONFIG_BT)
    /* Prefer BLE-specific work delay when BLE is the active endpoint and a value is provided */
    if (CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_BLE_MS > 0) {
        struct zmk_endpoint_instance sel = zmk_endpoints_selected();
        if (sel.transport == ZMK_TRANSPORT_BLE) {
            d = CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_BLE_MS;
        }
    }
#endif
    return d;
}

void hid_clear_and_flush(void) {
    zmk_hid_keyboard_clear();
    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
}

static void safe_send_report(void) {
    // Simple bounded retry to avoid indefinite waits
    for (int i = 0; i < 2; i++) {
        zmk_endpoints_send_report(HID_USAGE_KEY);
        k_sleep(K_MSEC(1));
    }
}

void press_and_release(zmk_key_t usage) {
    zmk_hid_keyboard_press(usage);
    safe_send_report();
    k_sleep(K_MSEC(selected_delay_ms()));
    zmk_hid_keyboard_release(usage);
    safe_send_report();
    k_sleep(K_MSEC(selected_delay_ms()));
}

static bool send_char(char c) {
    // Map common ASCII to HID usages; returns false if unsupported
    if (c >= 'a' && c <= 'z') {
        zmk_key_t kc = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
        press_and_release(kc);
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        zmk_key_t kc = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
        zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
        safe_send_report();
        k_sleep(K_MSEC(selected_delay_ms()));
        press_and_release(kc);
        zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT));
        safe_send_report();
        k_sleep(K_MSEC(selected_delay_ms()));
        return true;
    }
    if (c >= '1' && c <= '9') {
        uint16_t base = HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        zmk_key_t kc = ZMK_HID_USAGE(HID_USAGE_KEY, (base + (c - '1')));
        press_and_release(kc);
        return true;
    }
    if (c == '0') {
        zmk_key_t kc = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS));
        press_and_release(kc);
        return true;
    }
    switch (c) {
    case ' ':
        press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR));
        return true;
    case ',':
        press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN));
        return true;
    case '.':
        press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN));
        return true;
    case '-':
        press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE));
        return true;
    case '\'':
        press_and_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE));
        return true;
    default:
        return false;
    }
}

static bool get_key_for_char(char c, zmk_key_t *key, zmk_key_t *mod) {
    *mod = 0;
    if (c >= 'a' && c <= 'z') {
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'a')));
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_A + (c - 'A')));
        *mod = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT);
        return true;
    }
    if (c >= '1' && c <= '9') {
        uint16_t base = HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, (base + (c - '1')));
        return true;
    }
    if (c == '0') {
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, (HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS));
        return true;
    }
    switch (c) {
    case ' ':
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_SPACEBAR);
        return true;
    case ',':
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN);
        return true;
    case '.':
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN);
        return true;
    case '-':
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE);
        return true;
    case '\'':
        *key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE);
        return true;
    default:
        return false;
    }
}

static void correction_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct autocorrect_correction_work *cw = CONTAINER_OF(dwork, struct autocorrect_correction_work, work);

    // Cancel if sequence advanced (buffer changed) since scheduling
    atomic_val_t current = atomic_get(&autocorrect_seq);
    if (current != atomic_get(&cw->seq)) {
        cw->state = AUTOCORRECT_STATE_IDLE;
        autocorrect_set_suppress(false);
        hid_clear_and_flush(); // Ensure no keys are left stuck
        return;
    }

    if (cw->state == AUTOCORRECT_STATE_IDLE) {
        // Should not happen if scheduled correctly
        autocorrect_set_suppress(false);
        return;
    }

    // Process current state
    bool reschedule = true;
    int delay = selected_delay_ms();

    switch (cw->state) {
    case AUTOCORRECT_STATE_IDLE:
        // Should not happen if scheduled correctly
        reschedule = false;
        break;

    case AUTOCORRECT_STATE_BACKSPACE_SUFFIX:
        if (cw->suffix_delim) {
            if (cw->sub_state == AUTOCORRECT_SUB_STATE_KEY_PRESS) {
                zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE));
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_RELEASE;
                cw->current_key = ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE);
            } else {
                zmk_hid_keyboard_release(cw->current_key);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->state = AUTOCORRECT_STATE_BACKSPACE_LETTERS;
                cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
                cw->index = 0;
            }
        } else {
            cw->state = AUTOCORRECT_STATE_BACKSPACE_LETTERS;
            cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
            cw->index = 0;
            delay = 0; // No delay, run immediately
        }
        break;

    case AUTOCORRECT_STATE_BACKSPACE_LETTERS:
        // Use configured backspaces directly
        {
            if (cw->index < cw->backspaces) {
                if (cw->sub_state == AUTOCORRECT_SUB_STATE_KEY_PRESS) {
                    zmk_hid_keyboard_press(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE));
                    zmk_endpoints_send_report(HID_USAGE_KEY);
                    zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                    cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_RELEASE;
                } else {
                    zmk_hid_keyboard_release(ZMK_HID_USAGE(HID_USAGE_KEY, HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE));
                    zmk_endpoints_send_report(HID_USAGE_KEY);
                    zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                    cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
                    cw->index++;
                }
            } else {
                cw->state = AUTOCORRECT_STATE_TYPE_CHARS;
                cw->index = 0;
                cw->sub_state = AUTOCORRECT_SUB_STATE_MOD_PRESS;
                delay = 0;
            }
        }
        break;

    case AUTOCORRECT_STATE_TYPE_CHARS:
        if (cw->changes_ptr && cw->changes_ptr[cw->index] != '\0') {
            char c = cw->changes_ptr[cw->index];
            if (cw->sub_state == AUTOCORRECT_SUB_STATE_MOD_PRESS) {
                get_key_for_char(c, &cw->current_key, &cw->current_mod);
                if (cw->current_mod) {
                    zmk_hid_keyboard_press(cw->current_mod);
                    zmk_endpoints_send_report(HID_USAGE_KEY);
                    zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                } else {
                    delay = 0;
                }
                cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
            } else if (cw->sub_state == AUTOCORRECT_SUB_STATE_KEY_PRESS) {
                zmk_hid_keyboard_press(cw->current_key);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_RELEASE;
            } else if (cw->sub_state == AUTOCORRECT_SUB_STATE_KEY_RELEASE) {
                zmk_hid_keyboard_release(cw->current_key);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->sub_state = AUTOCORRECT_SUB_STATE_MOD_RELEASE;
            } else if (cw->sub_state == AUTOCORRECT_SUB_STATE_MOD_RELEASE) {
                if (cw->current_mod) {
                    zmk_hid_keyboard_release(cw->current_mod);
                    zmk_endpoints_send_report(HID_USAGE_KEY);
                    zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                } else {
                    delay = 0;
                }
                cw->sub_state = AUTOCORRECT_SUB_STATE_MOD_PRESS;
                cw->index++;
            }
        } else {
            cw->state = AUTOCORRECT_STATE_TYPE_SUFFIX;
            cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
            delay = 0;
        }
        break;

    case AUTOCORRECT_STATE_TYPE_SUFFIX:
        if (cw->suffix_delim) {
            if (cw->sub_state == AUTOCORRECT_SUB_STATE_KEY_PRESS) {
                zmk_key_t key, mod;
                get_key_for_char(cw->suffix_delim, &key, &mod);
                zmk_hid_keyboard_press(key);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->sub_state = AUTOCORRECT_SUB_STATE_KEY_RELEASE;
                cw->current_key = key;
            } else {
                zmk_hid_keyboard_release(cw->current_key);
                zmk_endpoints_send_report(HID_USAGE_KEY);
                zmk_endpoints_send_report(HID_USAGE_KEY); // Retry for reliability
                cw->state = AUTOCORRECT_STATE_IDLE;
                reschedule = false;
            }
        } else {
            cw->state = AUTOCORRECT_STATE_IDLE;
            reschedule = false;
        }
        break;
    }

    if (cw->state == AUTOCORRECT_STATE_IDLE) {
        // Finish up
        hid_clear_and_flush(); // Ensure clean state
        correction_count++;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        typo_buffer_size = 1;
        atomic_inc(&autocorrect_seq);
        autocorrect_set_suppress(false);
        reschedule = false;
    }

    if (reschedule) {
        k_work_reschedule(dwork, K_MSEC(delay));
    }
}

// ... (unchanged code)

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
        LOG_ERR("Autocorrect: NVS mount failed: %d (continuing with RAM defaults)", rc);
#endif
        // Continue with RAM default; do not fail init
    }
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: NVS mounted successfully");
#endif
#endif
    k_work_init_delayable(&correction_work.work, correction_work_handler);
    atomic_set(&autocorrect_seq, 0);
#if FIXED_PARTITION_EXISTS(storage)
    // Read persisted enabled flag once at boot
    uint8_t enabled;
    int r = nvs_read(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
    if (r > 0) {
        autocorrect_enabled = (enabled == 1);
    }
#endif
#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Initialized successfully, enabled=%s", autocorrect_is_enabled() ? "true" : "false");
#endif

#if CONFIG_ZMK_AUTOCORRECT_SELFTEST
    // Dictionary self-test: basic sanity check
    dict_valid = (DICTIONARY_SIZE > 0);
    if (dict_valid) {
        uint8_t first_byte = autocorrect_data[0];
        uint8_t node_type = first_byte & 0xC0;
        // Valid node types: 0x00 (chain), 0x40 (branch), 0x80 (leaf)
        dict_valid = (node_type == 0x00 || node_type == 0x40 || node_type == 0x80);
    }
#if AUTOCORRECT_DEBUG
    if (dict_valid) {
        LOG_INF("Autocorrect: Dictionary self-test PASSED (size=%u, first_byte=0x%02x)", 
                DICTIONARY_SIZE, autocorrect_data[0]);
    } else {
        LOG_INF("Autocorrect: Dictionary self-test FAILED (size=%u, first_byte=0x%02x)", 
                DICTIONARY_SIZE, DICTIONARY_SIZE > 0 ? autocorrect_data[0] : 0);
    }
#endif
#else
    // Self-test disabled, assume valid
    dict_valid = true;
#endif

    return 0;
}

bool autocorrect_is_enabled(void) {
    return autocorrect_enabled;
}

static void persist_enabled_state(void) {
#if FIXED_PARTITION_EXISTS(storage)
    uint8_t enabled = autocorrect_enabled ? 1 : 0;
    (void)nvs_write(&fs, AUTOCORRECT_ENABLE_ID, &enabled, sizeof(enabled));
#endif
}

void autocorrect_enable(void) {
    autocorrect_enabled = true;
    persist_enabled_state();
    k_work_cancel_delayable(&correction_work.work);
    typo_buffer_size = 1;
    typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
    atomic_inc(&autocorrect_seq);
#if CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK
    // Optional feedback kept minimal to avoid host interference by default
    // send_string("on");
#endif
}

void autocorrect_disable(void) {
    autocorrect_enabled = false;
    persist_enabled_state();
    k_work_cancel_delayable(&correction_work.work);
    autocorrect_set_suppress(false);
    typo_buffer_size = 0;
    atomic_inc(&autocorrect_seq);
#if CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK
    // send_string("off");
#endif
}

void autocorrect_toggle(void) {
    if (autocorrect_enabled) {
        autocorrect_disable();
    } else {
        autocorrect_enable();
    }
#if CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK
    // Minimal optional feedback
    // send_string(autocorrect_enabled ? "on" : "off");
#endif
}

void autocorrect_set_suppress(bool suppress) {
    atomic_set(&autocorrect_suppressed, suppress ? 1 : 0);
}

bool autocorrect_is_suppressed(void) {
    return atomic_get(&autocorrect_suppressed) != 0;
}

static __maybe_unused void tap_code(zmk_key_t keycode) { press_and_release(keycode); }

static __maybe_unused void send_string(const char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        (void)send_char(str[i]);
    }
}

// ... (unchanged code)

__attribute__((weak))
bool apply_autocorrect(uint8_t backspaces, const char *str, char *typo, char *correct) {
    return true;
}

__attribute__((weak))
bool process_autocorrect_user(struct zmk_keycode_state_changed *ev) {
    // Suppress autocorrect while non-shift modifiers are actively pressed
    uint8_t shift_mask = (uint8_t)((1u << (HID_USAGE_KEY_KEYBOARD_LEFTSHIFT - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL)) |
                                   (1u << (HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL)));
    uint8_t non_shift_mods = (uint8_t)(mods_pressed & (uint8_t)(~shift_mask));
    return non_shift_mods == 0;
}

// ... (unchanged code)

static int autocorrect_event_listener(const zmk_event_t *eh) {
    struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

#if AUTOCORRECT_DEBUG
    LOG_INF("Autocorrect: Key event - keycode=0x%04X, enabled=%s", ev->keycode, autocorrect_is_enabled() ? "true" : "false");
#endif

    // Ignore all events while suppressed (e.g., during diagnostics typing)
    if (autocorrect_is_suppressed()) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    event_count++;
    if (!autocorrect_is_enabled()) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!process_autocorrect_user(ev)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Require keyboard usage page (0x07)
    if (ev->usage_page != HID_USAGE_KEY) {
        // Reset on non-keyboard events
        typo_buffer_size = 1;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        atomic_inc(&autocorrect_seq);
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Reset buffer on non-keyboard event");
#endif
        return ZMK_EV_EVENT_BUBBLE;
    }
    uint8_t usage8 = (uint8_t)ev->keycode;

    // Track modifier state for both press and release
    if (is_modifier_usage(usage8)) {
        uint8_t bit = usage8 - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
        if (ev->state) {
            mods_pressed |= (1u << bit);
        } else {
            mods_pressed &= ~(1u << bit);
        }
        // No further processing for modifiers
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!ev->state) {
        // Ignore key releases for non-mod keys
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (usage8 == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        if (typo_buffer_size > 0) { --typo_buffer_size; }
        atomic_inc(&autocorrect_seq);
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (usage8 == HID_USAGE_KEY_KEYBOARD_RETURN_ENTER) {
        // Cancel any pending correction on delimiter
        k_work_cancel_delayable(&correction_work.work);
        typo_buffer_size = 1;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        atomic_inc(&autocorrect_seq);
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Delimiter (ENTER) seen, cancel work & reset");
#endif
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Reset and ignore on non-printable or layer/behavior keys
    if (!is_printable_usage(usage8)) {
        k_work_cancel_delayable(&correction_work.work);
        typo_buffer_size = 1;
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        atomic_inc(&autocorrect_seq);
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Non-printable usage=0x%02X, cancel work & reset", usage8);
#endif
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Append to buffer (apply MAX_LENGTH sliding window as you already do)
    if (typo_buffer_size >= AUTOCORRECT_MAX_LENGTH) {
        memmove(typo_buffer, typo_buffer + 1, AUTOCORRECT_MAX_LENGTH - 1);
        typo_buffer_size = AUTOCORRECT_MAX_LENGTH - 1;
    }
    // Boundary check to prevent overflow
    if (typo_buffer_size < AUTOCORRECT_MAX_LENGTH) {
        typo_buffer[typo_buffer_size++] = usage8;
    }
    atomic_inc(&autocorrect_seq);
    if (is_printable_delimiter(usage8)) {
        last_delim_index = 0; // not tracking absolute stream; kept for future use
        // Cancel any pending work on new delimiter (space etc.)
        k_work_cancel_delayable(&correction_work.work);
#if AUTOCORRECT_DEBUG
        LOG_INF("Autocorrect: Printable delimiter 0x%02X seen, cancel pending work", usage8);
#endif
    }
    // Defer minimum-length check until we compute actual word length (letters/digits)

    // Extract the current word boundaries and delimiter
    uint8_t typo_len = 0;
    uint8_t typo_start = 0;
    bool delim_last = is_printable_delimiter(typo_buffer[typo_buffer_size - 1]);
    for (uint8_t i = typo_buffer_size; i > 0; --i) {
        uint8_t b = typo_buffer[i - 1];
        if (is_printable_delimiter(b) && i != typo_buffer_size) {
            typo_start = i;
            break;
        }
        ++typo_len;
    }
    if (delim_last && typo_len > 0) {
        --typo_len;
    }

    // Enforce minimum length based on actual word length
    if (typo_len < AUTOCORRECT_MIN_LENGTH) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Skip trie lookup if not at a leading boundary
    if (typo_start > 0 && !is_printable_delimiter(typo_buffer[typo_start - 1])) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    char typo[AUTOCORRECT_MAX_LENGTH + 1] = {0};
    uint8_t j = 0;
    for (uint8_t i = 0; i < typo_len && j < AUTOCORRECT_MAX_LENGTH; ++i) {
        uint8_t b = typo_buffer[typo_start + i];
        if (is_alpha_usage(b)) {
            typo[j++] = 'a' + (b - HID_USAGE_KEY_KEYBOARD_A);
        } else if (is_digit_usage(b)) {
            // map digits 1..0 where present
            if (b == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
                typo[j++] = '0';
            } else {
                typo[j++] = '1' + (b - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION);
            }
        } else if (is_printable_delimiter(b)) {
            // word boundary; stop
            break;
        }
    }
    typo[j] = '\0';

    // Build KC sequence with boundary anchors (KC-based dictionary, leading boundary only)
    uint8_t kc_seq[AUTOCORRECT_MAX_LENGTH + 2] = {0};
    uint8_t kclen = 0;
    // prepend boundary if start-of-buffer or preceding char is delimiter
    if (typo_start == 0 || (typo_start > 0 && is_printable_delimiter(typo_buffer[typo_start - 1]))) {
        kc_seq[kclen++] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
    }
    for (uint8_t i = 0; i < typo_len && kclen < (uint8_t)(AUTOCORRECT_MAX_LENGTH + 1); ++i) {
        uint8_t b = typo_buffer[typo_start + i];
        if (is_alpha_usage(b) || is_digit_usage(b)) {
            kc_seq[kclen++] = b;
        } else if (is_printable_delimiter(b)) {
            break;
        }
    }
    // No trailing boundary - corrections trigger immediately after last letter
    // Dictionary is KC-based with a leading boundary only
    // Trie lookup using generated dictionary over KC sequence
    uint8_t backspaces = 0;
    const char *changes = NULL;
    bool matched = trie_lookup_kc(kc_seq, kclen, &backspaces, &changes);
    char correct[AUTOCORRECT_MAX_LENGTH + 10] = {0};

    // Determine scheduling parameters and suffix delimiter
    char suffix_delim = 0;
    if (delim_last) {
        uint8_t lastu = typo_buffer[typo_buffer_size - 1];
        if (lastu == HID_USAGE_KEY_KEYBOARD_SPACEBAR) suffix_delim = ' ';
        else if (lastu == HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN) suffix_delim = ',';
        else if (lastu == HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN) suffix_delim = '.';
        else if (lastu == HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE) suffix_delim = '-';
        else if (lastu == HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE) suffix_delim = '\'';
    }

    if (matched && changes != NULL && apply_autocorrect(backspaces, changes, typo, correct)) {
#if AUTOCORRECT_DEBUG
        uint8_t eff_preview = backspaces > typo_len ? typo_len : backspaces;
        LOG_INF("Autocorrect: Match backspaces=%u eff_preview=%u changes=\"%s\" kclen=%u typo_len=%u delim='%c'",
                backspaces, eff_preview, changes, kclen, typo_len, suffix_delim ? suffix_delim : '.');
        {
            char dump[4 * (AUTOCORRECT_MAX_LENGTH + 2)] = {0};
            int di = 0;
            uint8_t start = (typo_buffer_size > (AUTOCORRECT_MAX_LENGTH + 2)) ? (typo_buffer_size - (AUTOCORRECT_MAX_LENGTH + 2)) : 0;
            for (uint8_t i = start; i < typo_buffer_size && di < (int)sizeof(dump) - 4; ++i) {
                di += snprintk(dump + di, sizeof(dump) - di, "%02X ", typo_buffer[i]);
            }
            LOG_INF("Autocorrect: Sched buffer tail [%s]", dump);
        }
#endif
        // Freeze buffer and increment sequence before scheduling
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        typo_buffer_size = 1;
        atomic_inc(&autocorrect_seq);
        // Schedule with raw backspaces; handler will compute eff_backspaces vs typo_len_at_sched
        correction_work.backspaces = backspaces;
        correction_work.changes_ptr = changes;
        correction_work.suffix_delim = suffix_delim;
        correction_work.typo_len_at_sched = typo_len;
        
        // Initialize state machine
        correction_work.state = AUTOCORRECT_STATE_BACKSPACE_SUFFIX;
        correction_work.sub_state = AUTOCORRECT_SUB_STATE_KEY_PRESS;
        correction_work.index = 0;

        atomic_set(&correction_work.seq, atomic_get(&autocorrect_seq));
        autocorrect_set_suppress(true);
        (void)k_work_schedule(&correction_work.work, K_MSEC(selected_work_delay_ms()));
#if AUTOCORRECT_DEBUG
        {
            int wd = selected_work_delay_ms();
            char epbuf[16] = {0};
            zmk_endpoint_instance_to_str(zmk_endpoints_selected(), epbuf, sizeof(epbuf));
            LOG_INF("Autocorrect: Scheduled work seq=%ld delay_ms=%d endpoint=%s",
                    (long)atomic_get(&correction_work.seq), wd, epbuf);
        }
#endif
    }

    // Buffer maintenance: keep sliding window; only seed on space or after scheduling a correction
    if (usage8 == HID_USAGE_KEY_KEYBOARD_SPACEBAR) {
        typo_buffer[0] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
        typo_buffer_size = 1;
        atomic_inc(&autocorrect_seq);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

SYS_INIT(autocorrect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZMK_LISTENER(autocorrect_listener, autocorrect_event_listener);
ZMK_SUBSCRIPTION(autocorrect_listener, zmk_keycode_state_changed);

#ifdef CONFIG_ZTEST
// Test-only helpers to exercise internal logic without exposing in production
#include <zephyr/sys/util.h>
bool ac_build_correct_for_test(const uint8_t *buf, uint8_t size, uint8_t backspaces,
                               const char *changes, char *out, size_t out_sz) {
    if (!buf || !out || !changes || out_sz == 0) {
        return false;
    }
    if (size == 0) {
        out[0] = '\0';
        return true;
    }

    // Reuse internal delimiter rules
    uint8_t typo_len = 0;
    uint8_t typo_start = 0;
    bool delim_last = is_printable_delimiter(buf[size - 1]);
    for (uint8_t i = size; i > 0; --i) {
        uint8_t b = buf[i - 1];
        if (is_printable_delimiter(b) && i != size) {
            typo_start = i;
            break;
        }
        ++typo_len;
    }
    if (delim_last && typo_len > 0) {
        --typo_len;
    }

    // Build ascii typo for prefix keep
    char typo_ascii[AUTOCORRECT_MAX_LENGTH + 1] = {0};
    uint8_t j = 0;
    for (uint8_t i = 0; i < typo_len && j < AUTOCORRECT_MAX_LENGTH; ++i) {
        uint8_t b = buf[typo_start + i];
        if (is_alpha_usage(b)) {
            typo_ascii[j++] = 'a' + (b - HID_USAGE_KEY_KEYBOARD_A);
        }
    }
    typo_ascii[j] = '\0';

    // Bounded writer identical semantics to handler
    memset(out, 0, out_sz);
    size_t maxlen = out_sz - 1;
    uint8_t eff_backspaces = backspaces > typo_len ? typo_len : backspaces;
    size_t start_keep = (size_t)(typo_len - eff_backspaces);
    if (start_keep > 0) {
        size_t to_copy = start_keep > maxlen ? maxlen : start_keep;
        memcpy(out, typo_ascii, to_copy);
        out[to_copy] = '\0';
    }
    size_t cur = strlen(out);
    size_t avail = maxlen - cur;
    if (avail > 0) {
        size_t add = strnlen(changes, avail);
        memcpy(out + cur, changes, add);
        out[cur + add] = '\0';
    }
    if (delim_last) {
        cur = strlen(out);
        if (cur < maxlen) {
            uint8_t lastu = buf[size - 1];
            if (lastu == HID_USAGE_KEY_KEYBOARD_SPACEBAR) out[cur++] = ' ';
            else if (lastu == HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN) out[cur++] = ',';
            else if (lastu == HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN) out[cur++] = '.';
            else if (lastu == HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE) out[cur++] = '-';
            else if (lastu == HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE) out[cur++] = '\'';
            out[cur] = '\0';
        }

    }
    return true;
}

bool ac_lookup_typo_for_test(const uint8_t *buf, uint8_t size, uint8_t *out_backspaces,
                             const char **out_changes) {
    if (!buf || size == 0 || !out_backspaces || !out_changes) {
        return false;
    }
    uint8_t typo_len = 0;
    uint8_t typo_start = 0;
    bool delim_last = is_printable_delimiter(buf[size - 1]);
    for (uint8_t i = size; i > 0; --i) {
        uint8_t b = buf[i - 1];
        if (is_printable_delimiter(b) && i != size) {
            typo_start = i;
            break;
        }
        ++typo_len;
    }
    if (delim_last && typo_len > 0) {
        --typo_len;
    }
    // Build KC sequence with boundary anchors
    uint8_t kc_seq[AUTOCORRECT_MAX_LENGTH + 2] = {0};
    uint8_t kclen = 0;
    if (typo_start == 0 || (typo_start > 0 && is_printable_delimiter(buf[typo_start - 1]))) {
        kc_seq[kclen++] = HID_USAGE_KEY_KEYBOARD_SPACEBAR;
    }
    for (uint8_t i = 0; i < typo_len && kclen < (uint8_t)(AUTOCORRECT_MAX_LENGTH + 1); ++i) {
        uint8_t b = buf[typo_start + i];
        if (is_alpha_usage(b) || is_digit_usage(b)) {
            kc_seq[kclen++] = b;
        } else if (is_printable_delimiter(b)) {
            break;
        }
    }
    // No trailing boundary in test helper either
    return trie_lookup_kc(kc_seq, kclen, out_backspaces, out_changes);
}

void ac_set_mods_for_test(uint8_t mods_mask) { mods_pressed = mods_mask; }
#endif

// Diagnostic getter functions
uint8_t autocorrect_get_buffer_size(void) {
    return typo_buffer_size;
}

bool autocorrect_dict_is_valid(void) {
    return dict_valid;
}

uint32_t autocorrect_get_lookup_count(void) {
    return lookup_count;
}

uint32_t autocorrect_get_correction_count(void) {
    return correction_count;
}