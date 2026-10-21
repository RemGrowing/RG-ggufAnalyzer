# GGUF Analyzer Engine v0.2.0

C++20 GGUF structural analyzer with an optional CUDA backend for per-chunk byte statistics. The CUDA path is optional: builds without a CUDA compiler remain fully functional using the CPU backend.

## Preserved v0.1.0 functions

- GGUF v2/v3 header parsing, metadata key/value parsing, and tensor descriptor inventory.
- Tensor shape/element-count and supported storage-size estimates.
- Tensor range, alignment, duplicate-name, overlap, unsupported-size, and gap checks.
- Independent full-file physical scan with chunk-level read coverage records.
- Streaming SHA-256 (additional file pass; does not load the whole model into RAM).
- JSON report containing metadata, tensors, gaps, chunks, and issues.

## v0.2.0 additions

- Optional CUDA backend (`backend_cuda.cu`) for per-chunk 256-bin byte histograms.
- CPU implementation of the same statistics, used when CUDA is unavailable or a CUDA chunk fails.
- Per-file byte statistics: zero/nonzero counts, byte-value sum, min/max, 256-bin histogram, and Shannon entropy in bits/byte.
- Backend reporting (`auto`, `cpu`, `cuda`), GPU chunk count, scan elapsed time, and scan throughput.
- CUDA is detected at CMake configure time. `auto` selects CUDA when the executable was built with CUDA and a CUDA device is present.

## Scope and performance caveat

GPU acceleration applies to byte histogram/statistics during the physical scan. GGUF structural parsing and SHA-256 remain CPU operations. The GPU path copies each bounded chunk to device memory; this can be slower than CPU processing on some storage devices, chunk sizes, or data distributions. Compare `--backend cpu` and `--backend cuda` on the target PC before choosing a default. The reported scan throughput covers the physical scan loop (file reads plus byte statistics), not initial structural parsing or the separate SHA-256 pass.

This version does **not** decode quantized tensor values, compute per-tensor numerical distributions, or accelerate inference. A 100% physical scan means all file bytes were read in that scan; it does not prove the file is semantically valid or that every tensor value was decoded.

## Build on Windows

Use PowerShell from the project root in a Visual Studio x64 developer environment, or a normal PowerShell where CMake can locate MSVC:

```powershell
.\build.ps1
```

CMake checks for a CUDA compiler automatically. To force a CPU-only build:

```powershell
.\build.ps1 -Backend CPU
```

To explicitly request CUDA detection/build:

```powershell
.\build.ps1 -Backend CUDA
```

If CUDA Toolkit / `nvcc` or a compatible host compiler is not found, CMake reports that it is building CPU-only; it does not fabricate a GPU path.

Direct CMake commands:

```powershell
cmake -S . -B build -DGGUF_ENABLE_CUDA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

## Run

```powershell
.\run.ps1 -InputFile 'D:\models\model.gguf' -Backend auto -ChunkMiB 64 -OutputFile 'report.json'
```

Or call the executable directly:

```powershell
.\build\Release\gguf_analyzer.exe 'D:\models\model.gguf' --backend cpu --chunk-mib 64 --out cpu_report.json
.\build\Release\gguf_analyzer.exe 'D:\models\model.gguf' --backend cuda --chunk-mib 64 --out cuda_report.json
```

Options:

- `--out <path>`: output JSON path.
- `--chunk-mib <N>`: chunk size, 1–256 MiB (default 64). The scan buffer is bounded to this size.
- `--backend auto|cpu|cuda`: select byte-statistics backend. `cuda` transparently falls back to CPU if unavailable or a chunk fails, with a warning in `issues`.
- `--no-hash`: skip SHA-256.
- `--help`: show help.

## Smoke test

The repository includes `tests/minimal.gguf`, a 32-byte synthetic GGUF v3 file, and a PowerShell smoke test:

```powershell
.\tests\test_backend.ps1
```

The smoke test checks CPU, auto, and CUDA-request/fallback paths, JSON validity, full physical coverage, and histogram count consistency. It does not substitute for testing against a large real-world GGUF or compiling/running the CUDA path on the target NVIDIA system.

## Report fields added in v0.2.0

- `backend`: requested backend, active backend, CUDA availability, GPU chunk count.
- `byte_statistics`: zero/nonzero byte counts, sum, min/max, entropy, and histogram.
- `performance`: physical-scan elapsed seconds and MiB/s.
- Each `physical_scan.chunks[]` item also contains zero/nonzero counts and chunk entropy.

## Known limitations carried forward

- Tensor storage size is only estimated for the GGML types implemented in the existing engine; unknown layouts are flagged rather than guessed.
- `tensor_value_decode_status` remains `not_implemented`.
- Metadata values are emitted as printable strings rather than native JSON scalar/array types.
- `unparsed_gaps` can include valid padding; a gap is not automatically corruption.
