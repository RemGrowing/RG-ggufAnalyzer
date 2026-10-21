"""Verify general.file_type=15 is reported as Q4_K_M and tensor type counts are emitted."""
import json
import os
import struct
import subprocess
import sys
import tempfile

if len(sys.argv) != 2:
    raise SystemExit("Usage: python test_quant_profile.py PATH_TO_ENGINE")
engine = os.path.abspath(sys.argv[1])

with tempfile.TemporaryDirectory(prefix="gguf_profile_test_") as td:
    gguf_path = os.path.join(td, "q4_k_m_profile.gguf")
    report_path = os.path.join(td, "report.json")
    key = b"general.file_type"
    # GGUF v3 header: zero tensors, one UINT32 metadata item (type id 4), value 15.
    raw = bytearray(b"GGUF")
    raw += struct.pack("<IQQ", 3, 0, 1)
    raw += struct.pack("<Q", len(key)) + key
    raw += struct.pack("<II", 4, 15)
    raw += b"\0" * ((-len(raw)) % 32)
    with open(gguf_path, "wb") as f:
        f.write(raw)
    result = subprocess.run([engine, gguf_path, "--out", report_path, "--chunk-mib", "1", "--backend", "cpu"],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    with open(report_path, encoding="utf-8") as f:
        report = json.load(f)
    quant = report["quantization"]
    assert quant["declared_file_type_id"] == 15, quant
    assert quant["declared_profile"] == "Q4_K_M", quant
    assert quant["tensor_type_counts"] == {}, quant

print("Q4_K_M profile recognition test passed.")
