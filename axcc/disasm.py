#!/usr/bin/env python3
"""Disassemble AXIS ELF binary .text section"""
import struct, sys, subprocess

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/b1_axis"
with open(path, "rb") as f:
    data = f.read()

# Parse ELF header
e_entry = struct.unpack_from("<Q", data, 24)[0]
e_phoff = struct.unpack_from("<Q", data, 32)[0]
e_phnum = struct.unpack_from("<H", data, 56)[0]

# Find LOAD segments
for i in range(e_phnum):
    off = e_phoff + i * 56
    p_type, p_flags = struct.unpack_from("<II", data, off)
    p_offset = struct.unpack_from("<Q", data, off + 8)[0]
    p_vaddr = struct.unpack_from("<Q", data, off + 16)[0]
    p_filesz = struct.unpack_from("<Q", data, off + 32)[0]
    if p_type == 1 and (p_flags & 1):  # PT_LOAD + PF_X
        print(f"Code segment: vaddr=0x{p_vaddr:x} offset=0x{p_offset:x} size={p_filesz}")
        # Extract and disassemble
        code = data[p_offset:p_offset + p_filesz]
        with open("/tmp/axis_text.bin", "wb") as out:
            out.write(code)
        result = subprocess.run([
            "objdump", "-d", "-M", "intel", "-b", "binary", "-m", "i386:x86-64",
            f"--adjust-vma=0x{p_vaddr:x}", "/tmp/axis_text.bin"
        ], capture_output=True, text=True)
        print(result.stdout)
        break
