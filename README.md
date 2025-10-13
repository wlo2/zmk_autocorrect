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
  - `CONFIG_ZMK_AUTOCORRECT_FAST_USB_MS=15` (optional lower delay when USB is active)
  - `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=10` (delay before correction starts)

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

- **Corrections don't appear or are cut off:** Increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
- **Corrections appear delayed or out of order:** Check BLE connection quality; increase `CONFIG_ZMK_AUTOCORRECT_DELAY_MS` to 40-50ms.
- **Corrections work on USB but not BLE:** This is now fixed; both transports are supported.
- **Fast typing causes missed corrections:** This is by design; overlapping corrections are prevented to avoid conflicts.

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

To use your own custom dictionary, you'll need to create a `autocorrect_data.h` file and place it in your ZMK config's `include` directory.

You can generate this file from a simple text file using the `qmk` command-line tool.

1.  **Create a dictionary file.**
    Create a file named `autocorrect_dict.txt` with your typos and corrections. The format is `typo -> correction`, one per line. For example:
    ```
    teh -> the
    wrok -> work
    ```

2.  **Generate the `autocorrect_data.h` file.**
    Run the following command to generate the header file:
    ```
    qmk generate-autocorrect-data autocorrect_dict.txt
    ```

3.  **Place the file in your ZMK config.**
    Copy the generated `autocorrect_data.h` file to the `include` directory of your ZMK config repository.

## Notes

- Boundaries: punctuation such as `, . - ' ` and space are treated as word delimiters and preserved after correction (e.g. `teh,` → `the,`).
- For splits, ensure the behavior queue is sized appropriately on the central.
