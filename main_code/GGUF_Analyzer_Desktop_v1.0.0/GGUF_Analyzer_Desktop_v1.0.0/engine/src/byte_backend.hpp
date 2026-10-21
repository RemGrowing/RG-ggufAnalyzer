#pragma once
#include "gguf_engine.hpp"
#include <cstddef>
#include <string>

namespace gguf::backend {
bool cuda_available();
const char* cuda_name();
ByteStats analyze_cpu(const unsigned char* data, size_t size);
ByteStats analyze_cuda(const unsigned char* data, size_t size);
}
