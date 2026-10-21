# v0.2.0 validation record

Validation performed in the packaging environment:

- CMake configure with `GGUF_ENABLE_CUDA=ON`: passed; CMake correctly detected that no CUDA compiler was installed and selected the CPU-only build.
- C++ build with GCC 14.2 / C++20: passed. Existing reader code emits integer-conversion warnings under `-Wconversion`; no build errors.
- Synthetic GGUF v3 fixture: structural parser reports `parsed_with_checks`.
- Full-file scan coverage: 100% on the fixture.
- CPU and auto backend JSON: parsed successfully; histogram sum equals file size; zero + nonzero count equals file size; byte sum matches independent calculation.
- Explicit CUDA request without CUDA: returns a report with CPU fallback and a `CUDA_UNAVAILABLE_FALLBACK` warning, rather than claiming GPU execution.
- SHA-256 output: matched Python's independent `hashlib.sha256` for the fixture.

Not validated here:

- CUDA `.cu` compilation with the user's installed Windows CUDA Toolkit / MSVC combination.
- Runtime on the user's RTX 3060 Ti.
- Relative CPU vs GPU performance on a large GGUF stored on the user's disk.

Run `.\build.ps1` from the project root on the target Windows system to perform the native toolchain build, then use `.\benchmark.ps1 -InputFile 'D:\models\model.gguf'` to compare CPU and CUDA-requested runs. The report's `backend.active` and `backend.gpu_chunks_processed` fields identify whether CUDA actually processed chunks.
