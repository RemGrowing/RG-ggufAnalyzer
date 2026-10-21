"""Synthetic K-quant decoder regression tests. Usage: python test_quant_decode.py PATH_TO_ENGINE"""
import json
import os
import struct
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("Usage: python test_quant_decode.py PATH_TO_ENGINE")
engine = os.path.abspath(sys.argv[1])

def decode(td, name, raw, type_id, block_bytes):
    source = os.path.join(td, name + ".bin")
    target = os.path.join(td, name + ".json")
    with open(source, "wb") as f:
        f.write(raw)
    result = subprocess.run([engine, source, "--decode-at", "0", "--decode-type", str(type_id),
                             "--decode-total", "256", "--decode-count", "256", "--decode-out", target],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(name + ": " + result.stdout + result.stderr)
    with open(target, encoding="utf-8") as f:
        data = json.load(f)
    values = data["values"]
    assert len(values) == 256, (name, len(values), block_bytes)
    return values

with tempfile.TemporaryDirectory(prefix="gguf_quant_test_") as td:
    # Q3_K: hmask[32], qs[64], scales[12], d(fp16) at byte 108.
    q3 = bytearray(110)
    struct.pack_into("<e", q3, 108, 1.0)
    values = decode(td, "q3", q3, 11, 110)
    assert all(v == 128 for v in values), values[:8]

    # Q4_K: d=1, dmin=0, first two group scales=1; low/high nibbles are 1 and 2.
    q4 = bytearray(144)
    struct.pack_into("<ee", q4, 0, 1.0, 0.0)
    q4[4] = 1
    q4[5] = 1
    q4[16] = 0x21
    values = decode(td, "q4", q4, 12, 144)
    assert values[0] == 1 and values[32] == 2, values[:40]

    # Q5_K: d=1, dmin=0, scale groups 0/1 are 1; high-bit plane is zero.
    q5 = bytearray(176)
    struct.pack_into("<ee", q5, 0, 1.0, 0.0)
    q5[4] = 1
    q5[5] = 1
    q5[48] = 0x21
    values = decode(td, "q5", q5, 13, 176)
    assert values[0] == 1 and values[32] == 2, values[:40]

    # Q6_K: ql[128], qh[64], scales[16], d at byte 208.
    # Set q1=33: low nibble 1 plus high two bits 2 -> signed quant 33-32 = 1.
    q6 = bytearray(210)
    q6[0] = 1
    q6[128] = 2
    q6[192] = 1
    struct.pack_into("<e", q6, 208, 1.0)
    values = decode(td, "q6", q6, 14, 210)
    assert values[0] == 1, values[:8]

print("Q3_K, Q4_K, Q5_K and Q6_K synthetic dequantization tests passed.")
