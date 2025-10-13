#pragma once

#include <zmk/keys.h>

// Internal send helpers shared across autocorrect module components.
// These are not part of the public API and may change without notice.

void hid_clear_and_flush(void);
void press_and_release(zmk_key_t usage);
