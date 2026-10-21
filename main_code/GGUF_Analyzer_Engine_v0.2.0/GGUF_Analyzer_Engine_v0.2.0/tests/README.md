# Test plan (v0.1.0)
Before production use, test with:
1. Valid GGUF v2 and v3 fixtures from trusted local sources.
2. Truncated header, metadata, tensor descriptor, and tensor data.
3. Invalid magic, unsupported version, malformed string/array lengths.
4. Duplicate tensor names, out-of-range offsets, overlapping tensor ranges.
5. Unknown tensor type IDs and unusual metadata keys.
6. Files smaller than one chunk and files whose size is not divisible by chunk size.
7. Large files, interrupted reads, and paths containing non-ASCII characters.

This initial package does not include copyrighted model files or claim to have been validated against every GGUF architecture/type.
