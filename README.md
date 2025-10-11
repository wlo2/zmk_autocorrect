
# ZMK Autocorrect Module

This module adds an autocorrect feature to ZMK, similar to the one found in QMK.

## Features

- Automatically corrects typos as you type.
- Comes with a default dictionary of common typos.
- Allows you to use your own custom dictionary.
- Can be enabled or disabled on the fly.

## How to Use

1.  **Add the module to your ZMK build.**
    You'll need to add this module to your `west.yml` file and then run `west update`.

2.  **Enable the Kconfig option.**
    Add the following line to your `.conf` file:
    ```
    CONFIG_ZMK_AUTOCORRECT=y
    ```

3.  **Include the behaviors in your keymap file.**
    Add the following line at the top of your keymap file (after the includes):
    ```
    #include <behaviors/autocorrect.dtsi>
    ```

4.  **Add the keycodes to your keymap.**
    The following keycodes are available to control the autocorrect feature:
    - `&ac_on`: Enables autocorrect.
    - `&ac_off`: Disables autocorrect.
    - `&ac_togg`: Toggles autocorrect.

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
