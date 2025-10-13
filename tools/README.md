# Autocorrect Dictionary Generation Tools

This directory contains tools for generating custom autocorrect dictionaries for ZMK.

## Overview

The ZMK autocorrect feature uses a **KC-based trie format** where typos are encoded as sequences of HID usage codes (keycodes) rather than ASCII characters. This matches the `trie_lookup_kc()` function in `src/autocorrect.c`.

### Why KC-based?

- **Direct matching**: Compares against actual keypress events (HID usage codes)
- **No ASCII conversion**: Avoids character mapping complexity
- **Modifier-aware**: Shift/Ctrl automatically suppress autocorrect
- **Efficient**: Single lookup per keypress

## Quick Start

1. **Edit the typo list**: Open `tools/typo_list.txt` and add your typos in the format `typo:correction` (one per line, lowercase only).

2. **Run the generator**:
   ```bash
   python tools/generate_dictionary.py
   ```

3. **Rebuild firmware**: The generator creates `include/autocorrect_data.h` which overrides the default dictionary. Rebuild your firmware to use the new dictionary.

## Dictionary Format Details

### HID Usage Codes

The generator converts characters to HID usage codes:

- **Letters a-z**: 0x04-0x1D
- **Digits 1-9**: 0x1E-0x26
- **Digit 0**: 0x27
- **Space**: 0x2C (also used as boundary marker)
- **Comma**: 0x36
- **Period**: 0x37
- **Apostrophe**: 0x34
- **Hyphen**: 0x2D

### Boundary Markers

Each typo is wrapped with space keycodes (0x2C) as boundaries:
- Input: `teh`
- Encoded: `[0x2C, 0x17, 0x08, 0x0B, 0x2C]` (space, t, e, h, space)

This ensures typos only match as complete words, not as substrings.

### Node Types

The trie uses three node types:

1. **Chain nodes**: Sequential keycodes terminated by 0x00
   - Example: `0x17 0x08 0x00` (t, e, null)

2. **Branch nodes**: Multiple paths marked with `(0x40 | kc)` + 2-byte offset, terminated by 0x00
   - Example: `0x57 0x03 0x00 0x48 0x06 0x00 0x00` (branch to 'h' at +3, 'e' at +6)

3. **Leaf nodes**: Correction string marked with `(0x80 | backspace_count)` + null-terminated string
   - Example: `0x83 't' 'h' 'e' 0x00` (backspace 3 times, type "the")

## Validation

The generator includes built-in validation that simulates the `trie_lookup_kc()` traversal for each entry. If validation fails, the generator will report which typo failed and why.

## Troubleshooting

### Unsupported Characters

The generator only supports lowercase letters, digits, and basic punctuation (space, comma, period, apostrophe, hyphen). If you include unsupported characters, you'll get an error:

```
ValueError: Unsupported character: '!'
```

**Solution**: Remove unsupported characters or extend `char_to_kc()` to handle them.

### Encoding Errors

If validation fails with "offset out of bounds" or "KC not found", the trie encoding may be incorrect. This usually indicates a bug in the generator.

**Solution**: Report the issue with your `typo_list.txt` content.

### Dictionary Too Large

Each entry adds ~10-30 bytes depending on shared prefixes. A 100-entry dictionary typically uses 2-4 KB.

**Solution**: Reduce entries or increase firmware flash size.

## Limitations

- **Lowercase only**: Uppercase typos are not supported (shift suppresses autocorrect)
- **Length constraints**: Typos should be 5-10 characters (configurable via `AUTOCORRECT_MIN_LENGTH` and `AUTOCORRECT_MAX_LENGTH`)
- **Firmware size**: Large dictionaries increase firmware size
- **No regex**: Only exact string matching is supported

## Advanced

### QMK Compatibility

The trie format is compatible with QMK's autocorrect feature. You can use QMK's `autocorrect_data` generator and adapt the output, or vice versa.

### Extensions

To add support for more characters:

1. Add the character to `char_to_kc()` with its HID usage code
2. Add the character to `is_printable_delimiter()` in `src/autocorrect.c` if it should act as a word boundary
3. Rebuild and test

### Custom Corrections

The correction string can include any ASCII characters. For example:

```
dont:don't
cant:can't
```

The generator will encode the apostrophe in the correction string correctly.
