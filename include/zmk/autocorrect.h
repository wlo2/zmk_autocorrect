
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
