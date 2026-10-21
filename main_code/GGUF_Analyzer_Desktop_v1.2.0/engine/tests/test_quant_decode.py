"""Synthetic Q3_K/Q4_K decoder regression tests. Usage: python test_quant_decode.py PATH_TO_ENGINE"""
import json
import os
import struct
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("Usage: python test_quant_decode.py PATH_TO_ENGINE")
engine = os.path.abspath(sys.argv[1])

with tempfile.TemporaryDirectory(prefix="gguf_quant_test_") as td:
    # Q3_K layout: hmask[32], qs[64], scales[12], d(fp16) at byte 108.
    q3 = bytearray(110)
    struct.pack_into("<e", q3, 108, 1.0)
    q3_path = os.path.join(td, "q3.bin")
    q3_out = os.path.join(td, "q3.json")
    open(q3_path, "wb").write(q3)
    result = subprocess.run([engine, q3_path, "--decode-at", "0", "--decode-type", "11",
                             "--decode-total", "256", "--decode-count", "256", "--decode-out", q3_out],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    values = json.load(open(q3_out, encoding="utf-8"))["values"]
    assert len(values) == 256 and all(v == 128 for v in values), (values[:8], values[-8:])

    # Q4_K: d=1, dmin=0, first two group scales=1; first packed byte has low=1/high=2.
    q4 = bytearray(144)
    struct.pack_into("<ee", q4, 0, 1.0, 0.0)
    q4[4] = 1
    q4[5] = 1
    q4[16] = 0x21
    q4_path = os.path.join(td, "q4.bin")
    q4_out = os.path.join(td, "q4.json")
    open(q4_path, "wb").write(q4)
    result = subprocess.run([engine, q4_path, "--decode-at", "0", "--decode-type", "12",
                             "--decode-total", "256", "--decode-count", "256", "--decode-out", q4_out],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    values = json.load(open(q4_out, encoding="utf-8"))["values"]
    assert len(values) == 256 and values[0] == 1 and values[32] == 2, (values[:40])

print("Q3_K and Q4_K synthetic dequantization tests passed.")
