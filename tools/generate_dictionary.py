#!/usr/bin/env python3
"""
ZMK Autocorrect Dictionary Generator

Generates KC-based (HID usage code) autocorrect dictionaries from a typo list.
Output format matches the trie_lookup_kc() function in src/autocorrect.c.
"""

import sys
from pathlib import Path
from typing import List, Tuple, Dict, Optional


def char_to_kc(c: str) -> int:
    """Convert ASCII character to HID usage code."""
    if 'a' <= c <= 'z':
        return 0x04 + (ord(c) - ord('a'))
    elif '1' <= c <= '9':
        return 0x1E + (ord(c) - ord('1'))
    elif c == '0':
        return 0x27
    elif c == ' ':
        return 0x2C  # Space - used as boundary marker
    elif c == ',':
        return 0x36
    elif c == '.':
        return 0x37
    elif c == "'":
        return 0x34
    elif c == '-':
        return 0x2D
    else:
        raise ValueError(f"Unsupported character: '{c}'")


def typo_to_kc_seq(typo: str) -> List[int]:
    """Convert typo string to KC sequence with boundary markers."""
    seq = [0x2C]  # Prepend boundary (space)
    for c in typo.lower():
        seq.append(char_to_kc(c))
    seq.append(0x2C)  # Append boundary (space)
    return seq


class TrieNode:
    """Node in the autocorrect trie."""
    def __init__(self):
        self.children: Dict[int, TrieNode] = {}
        self.correction: Optional[str] = None
        self.backspaces: Optional[int] = None


def build_trie(entries: List[Tuple[str, str]]) -> TrieNode:
    """Build trie from list of (typo, correction) tuples."""
    root = TrieNode()
    
    for typo, correction in entries:
        kc_seq = typo_to_kc_seq(typo)
        node = root
        
        for kc in kc_seq:
            if kc not in node.children:
                node.children[kc] = TrieNode()
            node = node.children[kc]
        
        node.correction = correction
        node.backspaces = len(typo)
    
    return root


def encode_trie(root: TrieNode) -> bytearray:
    """Serialize trie to byte array matching ZMK format."""
    data = bytearray()
    
    def encode_node(node: TrieNode, data: bytearray) -> int:
        """Encode a node and return its offset."""
        offset = len(data)
        
        # If this is a leaf node (has correction)
        if node.correction:
            # Use the stored typo length (excluding boundaries)
            backspace_count = node.backspaces if node.backspaces is not None else 0
            
            # Leaf marker: 0x80 | backspace_count
            data.append(0x80 | min(backspace_count, 0x7F))
            
            # Null-terminated correction string
            data.extend(node.correction.encode('ascii'))
            data.append(0x00)
            
            return offset
        
        # If single child, use chain encoding
        if len(node.children) == 1:
            kc, child = next(iter(node.children.items()))
            
            # Write KC sequence until we hit a branch or leaf
            current = node
            while len(current.children) == 1 and not current.correction:
                kc, current = next(iter(current.children.items()))
                data.append(kc)
            
            # Terminate chain
            data.append(0x00)
            
            # Encode the child
            encode_node(current, data)
            
            return offset
        
        # Multiple children - branch encoding
        child_offsets = []
        for kc in sorted(node.children.keys()):
            child = node.children[kc]
            # Reserve space for branch entry (3 bytes: marker+kc, offset_low, offset_high)
            child_offsets.append((kc, len(data)))
            data.extend([0x00, 0x00, 0x00])
        
        # Terminate branch list
        data.append(0x00)
        
        # Encode children and backpatch offsets
        for kc, branch_offset in child_offsets:
            child_offset = encode_node(node.children[kc], data)
            
            # Write branch entry: (0x40 | kc), absolute offset
            data[branch_offset] = 0x40 | kc
            data[branch_offset + 1] = child_offset & 0xFF
            data[branch_offset + 2] = (child_offset >> 8) & 0xFF
        
        return offset
    
    encode_node(root, data)
    return data


def validate_dictionary(data: bytearray, entries: List[Tuple[str, str]]) -> bool:
    """Validate dictionary by simulating trie_lookup_kc() traversal (mirrors firmware logic)."""
    all_valid = True
    
    for typo, correction in entries:
        kc_seq = typo_to_kc_seq(typo)
        
        # Simulate trie traversal matching firmware trie_lookup_kc()
        state = 0
        pos = 0
        entry_valid = True
        
        while True:
            if state >= len(data):
                print(f"FAIL: '{typo}' - state {state} out of bounds")
                all_valid = False
                entry_valid = False
                break
            
            byte = data[state]
            node_type = byte & 0xC0  # 00=chain, 01=branch, 10=leaf
            
            # Leaf node
            if node_type == 0x80:
                if pos != len(kc_seq):
                    print(f"FAIL: '{typo}' - reached leaf at state {state} but pos={pos}, expected {len(kc_seq)}")
                    all_valid = False
                    entry_valid = False
                else:
                    backspaces = byte & 0x7F
                    # Read correction string
                    corr_start = state + 1
                    corr_end = corr_start
                    while corr_end < len(data) and data[corr_end] != 0x00:
                        corr_end += 1
                    found_correction = data[corr_start:corr_end].decode('ascii')
                    if found_correction != correction:
                        print(f"FAIL: '{typo}' - correction mismatch: expected '{correction}', got '{found_correction}'")
                        all_valid = False
                        entry_valid = False
                break
            
            # Branch node
            elif node_type == 0x40:
                if pos >= len(kc_seq):
                    print(f"FAIL: '{typo}' - need more input at branch (state={state}, pos={pos})")
                    all_valid = False
                    entry_valid = False
                    break
                
                want = kc_seq[pos] & 0x3F
                idx = state
                matched = False
                
                while idx < len(data):
                    key = data[idx] & 0x3F
                    if key == 0:
                        break
                    
                    if key == want:
                        offset_low = data[idx + 1]
                        offset_high = data[idx + 2]
                        state = offset_low | (offset_high << 8)  # Absolute offset
                        pos += 1
                        matched = True
                        break
                    
                    idx += 3
                
                if not matched:
                    print(f"FAIL: '{typo}' - KC {want:02x} not found in branch at state {state}")
                    all_valid = False
                    entry_valid = False
                    break
            
            # Chain node
            else:
                idx = state
                while idx < len(data):
                    key = data[idx]
                    if key == 0:
                        # Move to child node immediately after chain
                        state = idx + 1
                        break
                    
                    if pos >= len(kc_seq):
                        print(f"FAIL: '{typo}' - input shorter than chain at state {state}")
                        all_valid = False
                        entry_valid = False
                        break
                    
                    want = kc_seq[pos] & 0x3F
                    if (key & 0x3F) != want:
                        print(f"FAIL: '{typo}' - chain mismatch at state {state}: expected {want:02x}, got {key & 0x3F:02x}")
                        all_valid = False
                        entry_valid = False
                        break
                    
                    pos += 1
                    idx += 1
                
                if not entry_valid:
                    break
        
        if entry_valid:
            print(f"OK: '{typo}' -> '{correction}'")
    
    return all_valid


def generate_header(data: bytearray, entries: List[Tuple[str, str]]) -> str:
    """Generate C header file content."""
    min_length = min(len(typo) for typo, _ in entries) if entries else 0
    max_length = max(len(typo) for typo, _ in entries) if entries else 0
    
    lines = [
        "// Generated by tools/generate_dictionary.py",
        "// Do not edit manually",
        "",
        f"#define AUTOCORRECT_MIN_LENGTH {min_length}",
        f"#define AUTOCORRECT_MAX_LENGTH {max_length}",
        f"#define DICTIONARY_SIZE {len(data)}",
        "",
        f"static const uint8_t autocorrect_data[DICTIONARY_SIZE] = {{",
    ]
    
    # Format byte array with 12 bytes per line
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_bytes},")
    
    lines.append("};")
    lines.append("")
    
    return "\n".join(lines)


def main():
    """Main entry point."""
    script_dir = Path(__file__).parent
    input_file = script_dir / "typo_list.txt"
    output_file = script_dir.parent / "include" / "autocorrect_data.h"
    
    # Read input file
    if not input_file.exists():
        print(f"Error: {input_file} not found")
        sys.exit(1)
    
    entries = []
    with open(input_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            if ':' not in line:
                print(f"Warning: Line {line_num} invalid format (expected 'typo:correction')")
                continue
            
            typo, correction = line.split(':', 1)
            typo = typo.strip().lower()
            correction = correction.strip()
            
            if not typo or not correction:
                print(f"Warning: Line {line_num} has empty typo or correction")
                continue
            
            entries.append((typo, correction))
    
    if not entries:
        print("Error: No valid entries found in typo_list.txt")
        sys.exit(1)
    
    print(f"Loaded {len(entries)} entries from {input_file}")
    
    # Build trie
    root = build_trie(entries)
    
    # Encode trie
    data = encode_trie(root)
    print(f"Generated dictionary: {len(data)} bytes")
    
    # Validate
    print("\nValidating dictionary:")
    if not validate_dictionary(data, entries):
        print("\nValidation failed!")
        sys.exit(1)
    
    # Generate header
    header = generate_header(data, entries)
    
    # Write output
    with open(output_file, 'w') as f:
        f.write(header)
    
    print(f"\nGenerated {output_file}")
    print("Rebuild your firmware to use the new dictionary.")


if __name__ == '__main__':
    main()
