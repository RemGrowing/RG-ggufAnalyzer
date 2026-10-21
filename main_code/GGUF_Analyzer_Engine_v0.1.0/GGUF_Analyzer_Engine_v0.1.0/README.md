# GGUF Analyzer Engine v0.1.0

C++20 command-line foundation for full-file GGUF inventory, structural parsing, tensor range checks, chunk scanning, and JSON reporting.

## Current scope
- Parses GGUF v2/v3 headers, metadata key/value entries, array values, and all tensor descriptors.
- Preserves metadata type and a printable value representation.
- Calculates tensor element counts and expected storage sizes for common GGML tensor types.
- Scans the entire file in configurable chunks and records read coverage.
- Checks file-range bounds, alignment, duplicate tensor names, overlapping tensor ranges, and unparsed gaps.
- Writes a JSON report, including metadata, tensors, scan chunks, issues, and coverage.
- Does not load the entire file into RAM.

## Important limitations
- This is an initial structural engine, not yet a complete semantic or quantized-value analyzer.
- Tensor storage-size calculations are implemented for common GGML types; unknown types are reported as unsupported rather than guessed.
- Structural parsing is little-endian, matching standard GGUF files. A malformed header or unsupported version is an error.
- Chunk scanning proves the file was read in full during the scan, but does not imply every tensor value was numerically decoded.
- GGUF metadata values are parsed safely with bounds checks. The report records raw metadata type and value; raw byte offsets for each individual metadata scalar are not yet emitted.
- This tool does not execute model code or perform inference.

## Build (Windows / Visual Studio)
Open **x64 Native Tools Command Prompt for VS 2022** or a PowerShell session initialized with the x64 MSVC environment:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Executable: `build\\Release\\gguf_analyzer.exe` (multi-config generator) or `build\\gguf_analyzer.exe`.

## Usage
```powershell
.\build\Release\gguf_analyzer.exe "D:\models\model.gguf" --out report.json
.\build\Release\gguf_analyzer.exe "D:\models\model.gguf" --chunk-mib 64 --out report.json
```

Options:
- `--out <path>`: JSON output path (default: `gguf_report.json`)
- `--chunk-mib <N>`: full-file scan chunk size in MiB (default: 64; minimum 1)
- `--no-hash`: skip whole-file SHA-256 calculation (SHA-256 is enabled by default)
- `--help`: show help

SHA-256 is implemented internally. Full-file hashing is an additional full read; chunk scanning is a separate pass so the report distinguishes those operations.

## Report interpretation
- `physical_scan.coverage_percent == 100` means the scanner successfully read every byte in the file in this pass.
- `structure_status` and `tensor_value_decode_status` are separate. `not_implemented` tensor decoding must not be interpreted as complete numerical analysis.
- `unparsed_gaps` are byte ranges not claimed by the known header/metadata/tensor descriptor/tensor data structures. Some gaps are valid padding; they are not automatically corruption.
