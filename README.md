# ZMK Autocorrect Module
> [!WARNING]
> Not yet functional. Logic for detection of the typos is working, correction is not.

This module adds an autocorrect feature to ZMK, similar to the one found in QMK.

## Features

- Autocorrect is **enabled by default** when the keyboard boots.
- Correction **triggers immediately after typing the last letter** of a typo (no trailing space required).
- Corrections work over both **USB and BLE** connections.

## Configuration

- For longer corrections or large dictionaries, increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
  - Recommended: `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128` or higher.
  - Each synthetic keypress during a correction uses queue slots.
- **Timing configs**:
  - `CONFIG_ZMK_AUTOCORRECT_DELAY_MS=35` (BLE-friendly per-key delay)
  - `CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS=15` (optional lower per-key delay when USB is active; only when `CONFIG_USB_DEVICE_STACK` is enabled)
  - `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=150` (default delay before correction starts)
  - `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_BLE_MS=200` (optional BLE-specific work delay when `CONFIG_BT` is enabled)

### Work Delay Configuration (Critical for Immediate Corrections)

- **What it is**: `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS` is the delay between detecting a typo and starting the correction work.
- **What it is not**: This is NOT the delay between individual correction keypresses (that's `CONFIG_ZMK_AUTOCORRECT_DELAY_MS`).
- **Why it matters**: For immediate corrections (no trailing delimiter), the last typed character's HID report is sent after scheduling. The work delay must be long enough for that last report to reach and be processed by the host before backspaces begin.
- **Recommended values**:
  - USB: 50–75 ms
  - Bluetooth (BLE): 150–200 ms
  - Default: 150 ms (safe for both)
- **Troubleshooting**: If corrections are missing the last character (e.g., `becuase` → `becuas`), start with 150ms and increase in 50 ms steps if needed.

### Configuration for Split Keyboards

For split keyboards (including dongle-based setups), the shared `config/<keyboard>.conf` file applies to **all builds** (left, right, and dongle). Add `CONFIG_ZMK_AUTOCORRECT=y` to this shared config file.

**Important**: The autocorrect logic only runs on the central side (per `CMakeLists.txt`), but the config and devicetree bindings must be present on all halves for proper compilation. This is normal ZMK behavior for split keyboards.

Example (BLE-focused):

```
CONFIG_ZMK_AUTOCORRECT=y
CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128
CONFIG_ZMK_AUTOCORRECT_DELAY_MS=35
CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=150
CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_BLE_MS=200
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
When you type the last character of a typo in the dictionary, autocorrect:
- Detects the match immediately.
- Queues a correction with a small delay (default 150ms via `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS`) to let the current key complete.
- Sends backspaces to erase the typo.
- Types the correct word.

There's a small delay between each synthetic keypress for reliability, especially over BLE. Any additional typing within the work delay window will cancel the pending correction to avoid conflicts with continued typing; this is intentional. The work delay is the time between detecting a match and starting the correction, not the total correction time.

### Timing and Race Conditions

- **Event order**: keypress → autocorrect listener detects match → HID sends the last key → work delay elapses → correction executes.
- **Race risk**: If the work delay is too short, backspaces can begin before the host has processed the last character, resulting in a missing character in the output.
- **Fix**: Use sufficient work delay. Typical guidance: USB 50–75 ms, BLE 150–200 ms. Default is 150 ms.

## Troubleshooting

### Testing Prerequisites

**Important**: Before troubleshooting, ensure you're testing with typos that exist in your dictionary.

- The default dictionary contains 70 entries (see `include/autocorrect_data_default.h`)
- Common test entries: `teh` → `the`, `becuase` → `because`, `retrun` → `return`
- Test format: Type the typo only; corrections trigger immediately after the last letter (no delimiter required)
- Examples: `teh` → `the`, `becuase` → `because`, `retrun` → `return`
- When testing with diagnostics enabled, allow ~100ms after the typo before pressing the toggle; pressing it immediately can cancel the pending correction due to the intentional delay window.
- The dictionary encodes typos with a leading boundary marker only; matches can occur as prefixes of longer words.
- Random character sequences like "mmmmmm" will NOT trigger corrections unless explicitly in your dictionary
- Minimum word length equals the generated constant `AUTOCORRECT_MIN_LENGTH`, which is the minimum typo length (letters/digits only) found in your dictionary. There is no fixed boundary inflation.

### Basic Issues

- **Corrections don't appear or are cut off:** Increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
- **Corrections appear delayed or out of order:** Check BLE connection quality; increase `CONFIG_ZMK_AUTOCORRECT_DELAY_MS` to 40–50 ms.
- **Corrections work on USB but not BLE:** This is now fixed; both transports are supported.
- **Fast typing causes missed corrections:** This is by design; overlapping corrections are prevented to avoid conflicts.

### Timing Adjustments

If corrections are incomplete (missing the last character), increase `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS` (e.g., 150 → 200 ms). If fast typing cancels corrections, you can also increase the work delay slightly. Total correction time ≈ work_delay + (backspaces × key_delay) + (replacement_chars × key_delay). Do not go below ~50 ms on USB or ~150 ms on BLE.

### Systematic Diagnosis (Without Console Logging)

For GitHub Actions builds without console access, enable `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` and use the **toggle diagnostic modes** to identify issues:

1. **Verify compilation**: Check GitHub Actions build logs for `autocorrect.c.obj` compilation. Firmware size should increase by 2-4 KB.

2. **Test toggle behavior**: Single-press the toggle key. With diagnostics enabled, it should type "1" (enabled) or "0" (disabled). If nothing happens, the behavior isn't working or isn't bound correctly.

3. **Create minimal dictionary**: Start with `tools/typo_list.txt` containing only `teh:the`. Run the generator and rebuild.

4. **Check dictionary validity**: Triple-press the toggle key. It should type "y" (valid) or "n" (invalid). If "n", the dictionary encoding is broken.

5. **Test buffer accumulation**: Type "abc" then double-press the toggle. It should type "3" (buffer size). If "0", events aren't reaching the autocorrect listener.

6. **Test correction**: Type "teh" and pause briefly (~50–100ms). It should correct to "the" automatically. If it doesn't work, proceed to step 7.

7. **Check lookup attempts**: Type "teh" then wait ~50–100ms and quadruple-press the toggle. It should type a non-zero digit (lookup count modulo 10). If "0", leading-boundary detection is failing. If non-zero, correction scheduling may be failing.

For detailed testing procedures, see `TESTING.md`.

### Toggle Diagnostic Modes

The autocorrect toggle behavior supports multi-tap detection for runtime diagnostics when `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` is enabled:

- **Single press**: Toggle on/off, types "1" (on) or "0" (off)
- **Double press** (within 500ms): Types buffer size as digit (0-9, or "X" if >9). Note: Buffer includes a leading SPACEBAR boundary marker, so typing "abc" reports "4" (SPACE + a + b + c).
- **Triple press** (within 500ms): Types "y" (dictionary valid) or "n" (invalid)
- **Quadruple press** (within 500ms): Types lookup count digit (modulo 10)
- **Quintuple press** (within 500ms): Types correction count digit (modulo 10)

These diagnostic modes provide runtime feedback for GitHub Actions builds without requiring console logging or LED indicators.

**Note**: Diagnostic feedback is **disabled by default** to prevent unexpected typing in production. Enable it only for testing and debugging.

### Diagnostic Testing

- Diagnostic keypresses can interfere with pending corrections if pressed too soon after typing a typo.
- To observe corrections: type the typo, pause briefly (~50–100ms), then check diagnostics; or simply watch the correction complete on the host before toggling diagnostics.
- Delimiters (space/comma/period/minus/quote) are preserved only when you actually type them and are handled via `suffix_delim`.
- A suppression mechanism prevents diagnostic output from affecting autocorrect processing, but understanding the timing window helps explain cancellation behavior.

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
- The default 35 ms delay between keypresses follows ZMK macro behavior recommendations and can be reduced on USB with `CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS`.
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
- Boundary markers now use only a leading space keycode, enabling immediate corrections without a trailing delimiter.
- ASCII-based helpers and traversal were removed to avoid drift. Only the KC-based path is supported and compiled.
- Defining `TRIE_LOOKUP_ASCII` is explicitly rejected at compile time.

## Tradeoffs and Limitations

- **False positives**: Without trailing delimiters, the system cannot distinguish complete words from prefixes. If `the` is in your dictionary and you type `there`, it may attempt to correct after `the`.
- **Mitigation**: Prefer longer typos (5+ characters) and avoid common word prefixes in your dictionary.
- **Alternative**: If false positives are unacceptable, revert to delimiter-based matching by modifying the generator to add a trailing boundary and restoring the trailing boundary check in `src/autocorrect.c`.

## Notes

- Boundaries: punctuation such as `, . - ' ` and space are treated as word delimiters and preserved after correction (e.g. `teh,` → `the,`).
- For splits, ensure the behavior queue is sized appropriately on the central.
