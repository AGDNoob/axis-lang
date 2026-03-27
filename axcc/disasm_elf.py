#!/usr/bin/env python3
"""Disassemble AXIS ELF binaries (no section headers)."""
import struct, subprocess, sys, tempfile, os

path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench3"
with open(path, "rb") as f:
    data = f.read()

entry = struct.unpack_from("<Q", data, 24)[0]
# First LOAD segment
ph_off = struct.unpack_from("<Q", data, 32)[0]
ph_size = struct.unpack_from("<H", data, 54)[0]
seg_offset = struct.unpack_from("<Q", data, ph_off + 8)[0]
seg_vaddr = struct.unpack_from("<Q", data, ph_off + 16)[0]
seg_filesz = struct.unpack_from("<Q", data, ph_off + 32)[0]

code = data[int(seg_offset):int(seg_offset + seg_filesz)]
base = seg_vaddr

# Write raw code to temp file and disassemble with objdump
with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
    tmp.write(code)
    tmpname = tmp.name

try:
    result = subprocess.run(
        ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
         f"--adjust-vma={int(base)}", tmpname],
        capture_output=True, text=True
    )
    print(result.stdout)
finally:
    os.unlink(tmpname)
