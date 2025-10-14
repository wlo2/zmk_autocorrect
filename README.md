# ZMK Autocorrect Module

This module adds an autocorrect feature to ZMK, similar to the one found in QMK.

## Features

- Autocorrect is **enabled by default** when the keyboard boots.
- Correction **triggers when you type the last character** of a typo (e.g., typing "teh " will correct to "the " when you press space).
- Corrections work over both **USB and BLE** connections.

## Configuration

- For longer corrections or large dictionaries, increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
  - Recommended: `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128` or higher.
  - Each synthetic keypress during a correction uses queue slots.
- Timing configs:
  - `CONFIG_ZMK_AUTOCORRECT_DELAY_MS=35` (BLE-friendly default)
  - `CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS=15` (optional lower delay when USB is active; only available when `CONFIG_USB_DEVICE_STACK` is enabled)
  - `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=10` (delay before correction starts)

### Configuration for Split Keyboards

For split keyboards (including dongle-based setups), the shared `config/<keyboard>.conf` file applies to **all builds** (left, right, and dongle). Add `CONFIG_ZMK_AUTOCORRECT=y` to this shared config file.

**Important**: The autocorrect logic only runs on the central side (per `CMakeLists.txt`), but the config and devicetree bindings must be present on all halves for proper compilation. This is normal ZMK behavior for split keyboards.

Example (BLE-focused):

```
CONFIG_ZMK_AUTOCORRECT=y
CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128
CONFIG_ZMK_AUTOCORRECT_DELAY_MS=35
CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=10
```

Example (USB-focused):

```
CONFIG_ZMK_AUTOCORRECT=y
CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128
CONFIG_ZMK_AUTOCORRECT_DELAY_MS=25
CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS=12
```

Optional HID robustness (prevents stuck mod releases on some hosts):

```
CONFIG_ZMK_HID_SEPARATE_MOD_RELEASE_REPORT=y
```

## How It Works

Autocorrect monitors your typing and maintains a buffer of recent characters.  
When you type the last character of a word that matches a typo in the dictionary, autocorrect:
- Detects the match immediately.
- Queues a correction (with a small delay to let the current key complete).
- Sends backspaces to erase the typo.
- Types the correct word.

There's a small delay between each keypress for reliability, especially over BLE.

## Troubleshooting

### Critical Bug: Sequence Number Race Condition

**Symptom**: Buffer fills correctly, lookups are attempted (non-zero lookup count), but corrections never execute.

**Cause**: In `src/autocorrect.c` at line 670, the code increments `autocorrect_seq` immediately after scheduling a correction. This causes the work handler to detect a sequence mismatch and cancel the correction.

**Fix**: Remove the `atomic_inc(&autocorrect_seq);` statement at line 670 inside the `if (matched)` block. Keep the increment at line 674 (in the `else if` block).

**Verification**: After fix, type `teh ` (with space) and it should correct to `the `. Use quadruple-press diagnostic to verify lookup count is incrementing.

### Testing Prerequisites

**Important**: Before troubleshooting, ensure you're testing with typos that exist in your dictionary.

- The default dictionary contains 70 entries (see `include/autocorrect_data_default.h`)
- Common test entries: `teh` → `the`, `becuase` → `because`, `retrun` → `return` 
- Test format: Type the typo followed by a boundary character (space, comma, period)
- Example: Type `teh ` (with space) to trigger correction to `the ` 
- Random character sequences like "mmmmmm" will NOT trigger corrections unless explicitly in your dictionary
- Minimum word length: 3 characters (due to `AUTOCORRECT_MIN_LENGTH` = 5 including boundaries)

### Basic Issues

- **Corrections don't appear or are cut off:** Increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
- **Corrections appear delayed or out of order:** Check BLE connection quality; increase `CONFIG_ZMK_AUTOCORRECT_DELAY_MS` to 40-50ms.
- **Corrections work on USB but not BLE:** This is now fixed; both transports are supported.
- **Fast typing causes missed corrections:** This is by design; overlapping corrections are prevented to avoid conflicts.

### Timing Adjustments

If fast typing cancels corrections, slightly increase CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS (e.g., 15–20ms).

### Systematic Diagnosis (Without Console Logging)

For GitHub Actions builds without console access, enable `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` and use the **toggle diagnostic modes** to identify issues:

1. **Verify compilation**: Check GitHub Actions build logs for `autocorrect.c.obj` compilation. Firmware size should increase by 2-4 KB.

2. **Test toggle behavior**: Single-press the toggle key. With diagnostics enabled, it should type "1" (enabled) or "0" (disabled). If nothing happens, the behavior isn't working or isn't bound correctly.

3. **Create minimal dictionary**: Start with `tools/typo_list.txt` containing only `teh:the`. Run the generator and rebuild.

4. **Check dictionary validity**: Triple-press the toggle key. It should type "y" (valid) or "n" (invalid). If "n", the dictionary encoding is broken.

5. **Test buffer accumulation**: Type "abc" then double-press the toggle. It should type "3" (buffer size). If "0", events aren't reaching the autocorrect listener.

6. **Test correction**: Type "teh " (with space). It should correct to "the ". If it doesn't work, proceed to step 7.

7. **Check lookup attempts**: Type "teh " then quadruple-press the toggle. It should type a non-zero digit (lookup count modulo 10). If "0", boundary detection is failing. If non-zero, correction scheduling is failing.

For detailed testing procedures, see `TESTING.md`.

### Toggle Diagnostic Modes

The autocorrect toggle behavior supports multi-tap detection for runtime diagnostics when `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` is enabled:

- **Single press**: Toggle on/off, types "1" (on) or "0" (off)
- **Double press** (within 500ms): Types buffer size as digit (0-9, or "X" if >9). Note: Buffer includes a leading SPACEBAR boundary marker, so typing "abc" reports "4" (SPACE + a + b + c).
- **Triple press** (within 500ms): Types "y" (dictionary valid) or "n" (invalid)
- **Quadruple press** (within 500ms): Types lookup count digit (modulo 10)

These diagnostic modes provide runtime feedback for GitHub Actions builds without requiring console logging or LED indicators.

**Note**: Diagnostic feedback is **disabled by default** to prevent unexpected typing in production. Enable it only for testing and debugging.

## How to Use

- Autocorrect is **enabled by default** (no need to enable it manually).
- The `&ac_togg` keycode toggles it on/off if you want to disable it temporarily.
- Optional feedback: set `CONFIG_ZMK_AUTOCORRECT_TOGGLE_FEEDBACK=y` to type "on" or "off" when toggled. Disabled by default to avoid interfering with host state.

## Overriding / Temporarily Suppressing Autocorrect

- Holding any non-Shift modifier (Ctrl/Alt/GUI) suppresses autocorrect until released. This avoids correcting command shortcuts.
- Typical mod-tap behavior is respected; the check looks at current HID modifier state.

## Technical Notes

- Corrections are queued asynchronously using Zephyr's work queue system.
- This prevents interference with normal typing and ensures proper event ordering.
- The default 30ms delay between keypresses follows ZMK macro behavior recommendations.
- Corrections work over both USB and BLE using ZMK's transport-agnostic endpoints API.

## Customizing the Dictionary

This module includes a Python-based dictionary generator that creates KC-based (HID usage code) autocorrect dictionaries compatible with ZMK.

### Quick Start

1. **Edit the typo list**: Open `tools/typo_list.txt` and add your typos in the format `typo:correction` (one per line, lowercase only).
   ```
   teh:the
   wrok:work
   becuase:because
   ```

2. **Run the generator**:
   ```bash
   python tools/generate_dictionary.py
   ```

3. **Rebuild firmware**: The generator creates `include/autocorrect_data.h` which overrides the default dictionary. Rebuild your firmware to use the new dictionary.

### Recommendations

- **Start small**: Begin with a minimal dictionary (1-3 entries) to validate the feature works before expanding.
- **Test incrementally**: Add entries one at a time to isolate any problematic patterns.
- **Lowercase only**: The generator only supports lowercase letters, digits, and basic punctuation (space, comma, period, apostrophe, hyphen).

For detailed documentation on the dictionary format, validation, and troubleshooting, see `tools/README.md`.

### Compatibility Note (KC-based traversal)

The autocorrect engine traverses the dictionary using HID Keyboard usages (KC-based) with boundary anchors. The included generator produces dictionaries in the correct format for `trie_lookup_kc()`.

- The lookup uses keyboard usage codes for `A..Z`, digits, and treats space/comma/period/minus/quote as delimiters.
- Boundary markers (space keycodes) are automatically added by the generator to ensure typos only match as complete words.

## Notes

- Boundaries: punctuation such as `, . - ' ` and space are treated as word delimiters and preserved after correction (e.g. `teh,` → `the,`).
- For splits, ensure the behavior queue is sized appropriately on the central.
