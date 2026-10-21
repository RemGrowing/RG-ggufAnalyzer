#include "byte_backend.hpp"
#include <cmath>
#include <stdexcept>

namespace gguf::backend {
ByteStats analyze_cpu(const unsigned char* data, size_t size) {
    ByteStats s;
    if (size == 0) { s.min_byte = 0; return s; }
    for (size_t i=0; i<size; ++i) {
        const auto v = data[i];
        ++s.histogram[v];
        s.sum_bytes += v;
        if (v == 0) ++s.zero_bytes; else ++s.nonzero_bytes;
        if (v < s.min_byte) s.min_byte = v;
        if (v > s.max_byte) s.max_byte = v;
    }
    double entropy = 0.0;
    const double n = static_cast<double>(size);
    for (const auto count : s.histogram) if (count) {
        const double p = static_cast<double>(count) / n;
        entropy -= p * std::log2(p);
    }
    s.entropy_bits_per_byte = entropy;
    return s;
}
#ifndef GGUF_HAS_CUDA
bool cuda_available() { return false; }
const char* cuda_name() { return "CUDA unavailable (CPU fallback)"; }
ByteStats analyze_cuda(const unsigned char*, size_t) { throw std::runtime_error("CUDA backend was not compiled"); }
#endif
}
