# Testing Without Console Logging

Since GitHub Actions builds don't provide console access, use these verification methods to test the autocorrect feature.

**Note**: To use the diagnostic modes described below, enable `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` in your `.conf` file. This is disabled by default to prevent unexpected typing in production.

## 1. Build Verification

Check GitHub Actions logs for the autocorrect compilation:

```
Building C object modules/zmk-feature-autocorrect/CMakeFiles/..__zmk_autocorrect.dir/src/autocorrect.c.obj
```

Compare firmware sizes before/after enabling autocorrect. Expect an increase of **+2-4 KB** depending on dictionary size.

## 2. Toggle Behavior Diagnostic Modes

The autocorrect toggle behavior supports multi-tap detection to provide runtime diagnostics without console logging. **Requires `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y`**.

### Single Press
- **Action**: Press toggle once
- **Output**: Types "1" (enabled) or "0" (disabled) if diagnostics enabled; otherwise silent toggle
- **Confirms**: Behavior works and HID sending is functional

### Double Press (within 500ms)
- **Action**: Press toggle twice rapidly
- **Output**: Types buffer size as digit (0-9, or "X" if >9)
- **Confirms**: Keys are being accumulated in the autocorrect buffer
- **Diagnosis**: If always "0", events aren't reaching the listener

### Triple Press (within 500ms)
- **Action**: Press toggle three times rapidly
- **Output**: Types "y" (dictionary valid) or "n" (invalid)
- **Confirms**: Dictionary loaded and passed self-test
- **Diagnosis**: If "n", dictionary encoding is broken or self-test disabled

### Quadruple Press (within 500ms)
- **Action**: Press toggle four times rapidly
- **Output**: Types lookup count digit (modulo 10)
- **Confirms**: Trie lookups are being attempted
- **Diagnosis**: If always "0", boundary detection may be failing

## 3. Minimal Dictionary Test

Start with the simplest possible test case:

1. Create `tools/typo_list.txt` with a single entry:
   ```
   teh:the
   ```

2. Run the generator:
   ```bash
   python tools/generate_dictionary.py
   ```

3. Rebuild firmware

4. Test by typing: `teh ` (with space)

5. **Expected**: Corrects to "the "

6. **If fails**: Core logic issue - proceed with diagnostic modes

## 4. Incremental Expansion

If "teh" works, gradually expand:

1. Add `tset:test` - rebuild and test
2. Add `becuase:because` - rebuild and test
3. Continue adding entries one at a time

**Purpose**: Isolate which entry causes dictionary encoding issues.

## 5. Event Flow Diagnosis

### Test if Events Are Received

1. Type "abc"
2. Double-press toggle

**If types "0"**: Events not reaching listener (config issue)  
**If types "3"**: Events received and buffered correctly

### Test if Lookups Are Attempted

1. Type "teh "
2. Quadruple-press toggle

**If types "0"**: Lookups not attempted (boundary detection issue)  
**If types non-zero**: Lookups attempted (correction scheduling issue)

## 6. Systematic Elimination

Test each component in order:

| Component | Test Method | Success Indicator |
|-----------|-------------|-------------------|
| **Config** | Check build logs | `autocorrect.c.obj` compiled |
| **Behavior** | Single-press toggle | Types "1" or "0" |
| **Events** | Type "abc" + double-press | Types "3" |
| **Dictionary** | Triple-press toggle | Types "y" |
| **Lookup** | Type "teh " + quadruple-press | Types non-zero |
| **Correction** | Type "teh " | Corrects to "the " |

Stop at the first failure and investigate that component.

## 7. Edge Cases

Test boundary conditions:

### No Boundary
- **Input**: `teh` (no space)
- **Expected**: No correction (boundary not reached)

### Leading Boundary
- **Input**: ` teh ` (with leading space)
- **Expected**: Corrects to " the "

### Punctuation Boundary
- **Input**: `teh,` (comma after typo)
- **Expected**: Corrects to "the,"

### Uppercase Suppression
- **Input**: `TEH ` (all caps)
- **Expected**: No correction (shift suppresses autocorrect)

### Modifier Suppression
- **Input**: `Ctrl+teh ` (with modifier)
- **Expected**: No correction (modifiers suppress autocorrect)

## 8. Recommended Test Sequence

Follow this sequence for systematic diagnosis:

### Step 1: Verify Build
- Check GitHub Actions logs for `autocorrect.c.obj`
- Verify firmware size increased by 2-4 KB

### Step 2: Confirm Behavior
- Enable `CONFIG_ZMK_AUTOCORRECT_DIAGNOSTICS=y` for testing
- Single-press toggle
- Should type "1" or "0"
- If nothing happens: behavior not bound or not working

### Step 3: Create Minimal Dictionary
- Edit `tools/typo_list.txt` with only: `teh:the`
- Run generator
- Rebuild

### Step 4: Confirm Dictionary Valid
- Triple-press toggle
- Should type "y"
- If types "n": dictionary encoding broken

### Step 5: Confirm Buffer Fills
- Type "abc"
- Double-press toggle
- Should type "3"
- If types "0": events not reaching listener

### Step 6: Test Correction
- Type "teh " (with space)
- Should correct to "the "
- If fails: proceed to Step 7

### Step 7: Confirm Lookup Attempted
- Type "teh " again
- Quadruple-press toggle
- Should type non-zero
- If types "0": boundary detection failing
- If types non-zero: correction scheduling failing

### Step 8: Expand Dictionary
- Add more entries incrementally
- Test each addition
- Isolate problematic entries

## 9. Common Issues

### Issue: Nothing Happens
- **Diagnosis**: Single-press toggle
- **If no output**: Behavior not working or not bound
- **Solution**: Check keymap binding

### Issue: Toggle Works But No Correction
- **Diagnosis**: Double-press after typing
- **If types "0"**: Events not reaching listener
- **Solution**: Check config, verify `CONFIG_ZMK_AUTOCORRECT=y`

### Issue: Buffer Fills But No Correction
- **Diagnosis**: Quadruple-press after typing typo
- **If types "0"**: Boundary detection failing
- **If types non-zero**: Correction scheduling failing
- **Solution**: Review work scheduling logic

### Issue: Dictionary Invalid
- **Diagnosis**: Triple-press toggle types "n"
- **Solution**: Regenerate dictionary, check for encoding errors

## 10. Timing Adjustments

If corrections are inconsistent, adjust timing:

- **Debounce delay**: 150ms (line 49 in `behavior_autocorrect_toggle.c`)
- **Multi-tap window**: 500ms (for diagnostic modes)
- **Work delay**: 5ms (line 622 in `autocorrect.c`)

Increase work delay if corrections are being dropped on slower systems.

## 11. Without Diagnostic Modes

If you haven't implemented the diagnostic modes yet, use these basic tests:

1. **Build verification**: Check logs
2. **Minimal dictionary**: Single entry "teh:the"
3. **Type test**: Type "teh " and observe
4. **Binary search**: Enable/disable autocorrect to confirm it's the cause
5. **Incremental expansion**: Add entries one at a time

## Summary

The diagnostic modes provide runtime observability without console logging. Use them to systematically identify where the autocorrect logic fails, then focus debugging efforts on that specific component.
