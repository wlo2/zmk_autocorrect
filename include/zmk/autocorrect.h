
#pragma once

#include <zephyr/kernel.h>
#include <zmk/events/keycode_state_changed.h>

// Function to apply the autocorrection.
// Returns true if the correction was applied, false otherwise.
bool apply_autocorrect(uint8_t backspaces, const char *str, char *typo, char *correct);

// User-overridable function to process keycodes before autocorrect.
// Returns true to allow autocorrection, false to skip.
bool process_autocorrect_user(struct zmk_keycode_state_changed *ev);

// Functions to control and query the autocorrect status.
bool autocorrect_is_enabled(void);
void autocorrect_enable(void);
void autocorrect_disable(void);
void autocorrect_toggle(void);

// Diagnostic functions for runtime debugging
uint8_t autocorrect_get_buffer_size(void);
bool autocorrect_dict_is_valid(void);
uint32_t autocorrect_get_lookup_count(void);

#ifdef CONFIG_ZTEST
bool ac_build_correct_for_test(const uint8_t *buf, uint8_t size, uint8_t backspaces,
                               const char *changes, char *out, size_t out_sz);
void ac_set_mods_for_test(uint8_t mods_mask);
bool ac_lookup_typo_for_test(const uint8_t *buf, uint8_t size, uint8_t *out_backspaces,
                             const char **out_changes);
#endif
