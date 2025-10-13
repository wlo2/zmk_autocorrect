# ZMK Autocorrect Module

This module adds an autocorrect feature to ZMK, similar to the one found in QMK.

## Features

- Autocorrect is **enabled by default** when the keyboard boots.
- Correction **triggers when you type the last character** of a typo (e.g., typing "teh " will correct to "the " when you press space).
- Corrections work over both **USB and BLE** connections.

## Configuration

- For longer corrections or large dictionaries, increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
  - Recommended: `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE=128` or higher.
  - Each correction character uses queue slots.
- Optional timing configs:
  - `CONFIG_ZMK_AUTOCORRECT_DELAY_MS=30` (delay between keypresses; 30-40ms for BLE, can be lower for USB)
  - `CONFIG_ZMK_AUTOCORRECT_WORK_DELAY_MS=10` (delay before correction starts)

## How It Works

Autocorrect monitors your typing and maintains a buffer of recent characters.  
When you type the last character of a word that matches a typo in the dictionary, autocorrect:
- Detects the match immediately.
- Queues a correction (with a small delay to let the current key complete).
- Sends backspaces to erase the typo.
- Types the correct word.

There's a small delay (30ms by default) between each keypress for reliability, especially over BLE.

## Troubleshooting

- **Corrections don't appear or are cut off:** Increase `CONFIG_ZMK_BEHAVIORS_QUEUE_SIZE` in your `.conf` file.
- **Corrections appear delayed or out of order:** Check BLE connection quality; increase `CONFIG_ZMK_AUTOCORRECT_DELAY_MS` to 40-50ms.
- **Corrections work on USB but not BLE:** This is now fixed; both transports are supported.
- **Fast typing causes missed corrections:** This is by design; overlapping corrections are prevented to avoid conflicts.

## How to Use

- Autocorrect is **enabled by default** (no need to enable it manually).
- The `&ac_togg` keycode toggles it on/off if you want to disable it temporarily.
- You'll see "ON" or "OFF" typed when you press the toggle key (visual feedback).

## Overriding Autocorrect

- Pressing Ctrl or Alt before typing the last letter of a word temporarily disables autocorrect for that word.
- This works because the modifier check in the code clears the typo buffer when non-Shift modifiers are active.

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

## Overriding Autocorrect

If you need to type a word that is in the autocorrect dictionary without it being corrected, you can press the `Ctrl` or `Alt` key before typing the last letter of the word. This will temporarily disable autocorrect for that word.
