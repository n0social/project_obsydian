#!/usr/bin/env python3
"""
Extract Warden RSA public key modulus from WoW 3.3.5a executable.

The RSA-2048 public key consists of:
- Exponent: 0x010001 (65537) - always the same
- Modulus: 256 bytes - hardcoded in WoW.exe

This script parses the PE structure and searches only in data sections
to avoid finding x86 code instead of the actual cryptographic key.
"""

import sys
import struct

class PEParser:
    """Simple PE32 executable parser"""

    def __init__(self, data):
        self.data = data
        self.sections = []
        self.parse()

    def parse(self):
        """Parse PE headers and section table"""
        # Check DOS signature
        if self.data[:2] != b'MZ':
            raise ValueError("Not a valid PE file (missing MZ signature)")

        # Get offset to PE header (at 0x3C in DOS header)
        pe_offset = struct.unpack('<I', self.data[0x3C:0x40])[0]

        # Check PE signature
        if self.data[pe_offset:pe_offset+4] != b'PE\x00\x00':
            raise ValueError("Not a valid PE file (missing PE signature)")

        # Parse COFF header
        coff_offset = pe_offset + 4
        machine = struct.unpack('<H', self.data[coff_offset:coff_offset+2])[0]
        num_sections = struct.unpack('<H', self.data[coff_offset+2:coff_offset+4])[0]
        size_of_optional_header = struct.unpack('<H', self.data[coff_offset+16:coff_offset+18])[0]

        # Section headers start after optional header
        section_offset = coff_offset + 20 + size_of_optional_header

        # Parse section headers (40 bytes each)
        for i in range(num_sections):
            sec_start = section_offset + (i * 40)
            name = self.data[sec_start:sec_start+8].rstrip(b'\x00').decode('ascii', errors='ignore')
            virtual_size = struct.unpack('<I', self.data[sec_start+8:sec_start+12])[0]
            virtual_address = struct.unpack('<I', self.data[sec_start+12:sec_start+16])[0]
            raw_size = struct.unpack('<I', self.data[sec_start+16:sec_start+20])[0]
            raw_offset = struct.unpack('<I', self.data[sec_start+20:sec_start+24])[0]
            characteristics = struct.unpack('<I', self.data[sec_start+36:sec_start+40])[0]

            # Characteristics flags
            IMAGE_SCN_CNT_CODE = 0x00000020
            IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
            IMAGE_SCN_MEM_READ = 0x40000000
            IMAGE_SCN_MEM_WRITE = 0x80000000

            is_code = bool(characteristics & IMAGE_SCN_CNT_CODE)
            is_data = bool(characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
            is_readable = bool(characteristics & IMAGE_SCN_MEM_READ)

            self.sections.append({
                'name': name,
                'virtual_address': virtual_address,
                'virtual_size': virtual_size,
                'raw_offset': raw_offset,
                'raw_size': raw_size,
                'characteristics': characteristics,
                'is_code': is_code,
                'is_data': is_data,
                'is_readable': is_readable
            })

    def get_data_sections(self):
        """Get sections that contain data (not code)"""
        data_sections = []
        for sec in self.sections:
            # We want readable data sections, not code sections
            # Common data section names: .data, .rdata, .idata
            if sec['is_data'] and sec['is_readable'] and not sec['is_code']:
                data_sections.append(sec)
            # Also include sections explicitly named .rdata or .data
            elif sec['name'] in ['.rdata', '.data', '.idata']:
                data_sections.append(sec)
        return data_sections

def calculate_entropy(data):
    """Calculate Shannon entropy of byte sequence (0-8 bits)"""
    if not data:
        return 0.0

    # Count byte frequencies
    freq = [0] * 256
    for byte in data:
        freq[byte] += 1

    # Calculate entropy
    import math
    entropy = 0.0
    for count in freq:
        if count > 0:
            p = count / len(data)
            entropy -= p * math.log2(p)

    return entropy

def is_likely_rsa_modulus(data):
    """
    Apply heuristics to determine if data looks like an RSA modulus

    RSA modulus characteristics:
    - 256 bytes exactly
    - High entropy (appears random)
    - High bit of MSB typically set (> 0x80)
    - Not all zeros or repetitive patterns
    - No obvious x86 instruction sequences
    - No sequential byte patterns
    """
    if len(data) != 256:
        return False

    # Check entropy (should be > 7.5 for cryptographic data)
    entropy = calculate_entropy(data)
    if entropy < 7.0:
        return False

    # Check for non-zero bytes
    non_zero = sum(1 for b in data if b != 0)
    if non_zero < 240:  # At least 93% non-zero
        return False

    # Check byte variety
    unique_bytes = len(set(data))
    if unique_bytes < 120:  # At least 120 different byte values
        return False

    # Check for sequential patterns (e.g., 0x81, 0x82, 0x83, ...)
    # Real RSA modulus should NOT have long sequential runs
    max_sequential = 0
    current_sequential = 1
    for i in range(1, len(data)):
        if data[i] == (data[i-1] + 1) % 256:
            current_sequential += 1
            max_sequential = max(max_sequential, current_sequential)
        else:
            current_sequential = 1

    if max_sequential > 8:  # More than 8 consecutive sequential bytes is suspicious
        return False

    # Check for repetitive patterns (same byte repeated)
    max_repetition = 0
    current_repetition = 1
    for i in range(1, len(data)):
        if data[i] == data[i-1]:
            current_repetition += 1
            max_repetition = max(max_repetition, current_repetition)
        else:
            current_repetition = 1

    if max_repetition > 4:  # More than 4 identical bytes in a row is suspicious
        return False

    # Check for x86 code patterns (common instruction bytes)
    # MOV: 0x8B, 0x89, 0x88, 0x8A
    # PUSH: 0x50-0x57
    # POP: 0x58-0x5F
    # Common prologue: 0x55 (PUSH EBP), 0x8B, 0xEC (MOV EBP, ESP)
    code_patterns = [
        b'\x55\x8B\xEC',  # Standard function prologue
        b'\x8B\x44\x24',  # MOV EAX, [ESP+...]
        b'\x8B\x4C\x24',  # MOV ECX, [ESP+...]
        b'\xFF\x15',      # CALL [...]
        b'\xE8',          # CALL relative
    ]

    for pattern in code_patterns:
        if pattern in data[:64]:  # Check first 64 bytes
            return False

    # MSB should have high bit set (typical for RSA modulus)
    # In little-endian, this would be the LAST byte
    if data[-1] < 0x80:
        return False

    return True

def find_warden_modulus(exe_path):
    """
    Find Warden RSA modulus in WoW.exe by parsing PE structure
    and searching only in data sections.
    """

    with open(exe_path, 'rb') as f:
        data = f.read()

    print(f"[*] Loaded {len(data)} bytes from {exe_path}")

    # Parse PE structure
    try:
        pe = PEParser(data)
        print(f"[*] Found {len(pe.sections)} PE sections")
    except Exception as e:
        print(f"[!] Failed to parse PE: {e}")
        return None

    # Get data sections
    data_sections = pe.get_data_sections()
    print(f"[*] Identified {len(data_sections)} data sections:")
    for sec in data_sections:
        print(f"    {sec['name']:8} - offset 0x{sec['raw_offset']:08x}, size {sec['raw_size']:8} bytes")

    # Search for RSA exponent in data sections only
    exponent_pattern = b'\x01\x00\x01\x00'

    candidates = []

    for sec in data_sections:
        section_data = data[sec['raw_offset']:sec['raw_offset'] + sec['raw_size']]

        # Find exponent pattern in this section
        offset = 0
        while True:
            offset = section_data.find(exponent_pattern, offset)
            if offset == -1:
                break

            file_offset = sec['raw_offset'] + offset
            print(f"\n[*] Found exponent pattern at 0x{file_offset:08x} (section {sec['name']})")

            # Search for 256-byte modulus near this exponent
            # Try before and after the exponent
            search_range = 1024
            start = max(0, offset - search_range)
            end = min(len(section_data), offset + search_range)

            for mod_offset in range(start, end):
                if mod_offset + 256 > len(section_data):
                    break

                modulus_candidate = section_data[mod_offset:mod_offset + 256]

                if is_likely_rsa_modulus(modulus_candidate):
                    file_mod_offset = sec['raw_offset'] + mod_offset
                    entropy = calculate_entropy(modulus_candidate)

                    candidates.append({
                        'offset': file_mod_offset,
                        'section': sec['name'],
                        'data': modulus_candidate,
                        'entropy': entropy,
                        'exponent_offset': file_offset
                    })

            offset += 1

    # Sort candidates by entropy (higher is better)
    candidates.sort(key=lambda x: x['entropy'], reverse=True)

    if not candidates:
        print("\n[!] No RSA modulus candidates found")
        print("[!] The modulus might be obfuscated or in an unexpected format")
        return None

    print(f"\n[*] Found {len(candidates)} RSA modulus candidate(s)")

    for i, cand in enumerate(candidates[:3]):  # Show top 3
        print(f"\n{'='*70}")
        print(f"[+] Candidate #{i+1}")
        print(f"    File offset: 0x{cand['offset']:08x}")
        print(f"    Section: {cand['section']}")
        print(f"    Entropy: {cand['entropy']:.3f} bits/byte")
        print(f"    Near exponent at: 0x{cand['exponent_offset']:08x}")
        print(f"    First 32 bytes: {cand['data'][:32].hex()}")
        print(f"    Last 32 bytes:  {cand['data'][-32:].hex()}")

        if i == 0:
            print(f"\n[*] C++ array format (BEST CANDIDATE):")
            print_cpp_array(cand['data'])

    return candidates[0]['data'] if candidates else None

def print_cpp_array(data):
    """Print byte array in C++ format"""
    print("const uint8_t modulus[256] = {")
    for i in range(0, 256, 16):
        chunk = data[i:i+16]
        hex_bytes = ', '.join(f'0x{b:02X}' for b in chunk)
        comma = ',' if i < 240 else ''
        print(f"    {hex_bytes}{comma}")
    print("};")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path_to_Wow.exe>")
        sys.exit(1)

    exe_path = sys.argv[1]
    modulus = find_warden_modulus(exe_path)

    if modulus:
        print(f"\n[✓] Successfully extracted RSA modulus!")
        print(f"[*] Copy the C++ array above into warden_module.cpp")
    else:
        print(f"\n[✗] Failed to extract RSA modulus")
        sys.exit(1)
