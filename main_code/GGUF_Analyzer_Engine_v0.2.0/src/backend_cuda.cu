#include "byte_backend.hpp"
#include <cuda_runtime.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace gguf::backend {
namespace {
__global__ void histogram_kernel(const unsigned char* data, size_t n, unsigned int* hist) {
    __shared__ unsigned int local_hist[256];
    const unsigned int tid = threadIdx.x;
    local_hist[tid] = 0;
    __syncthreads();
    const size_t first = static_cast<size_t>(blockIdx.x) * blockDim.x + tid;
    const size_t stride = static_cast<size_t>(gridDim.x) * blockDim.x;
    for (size_t i = first; i < n; i += stride) atomicAdd(&local_hist[data[i]], 1u);
    __syncthreads();
    if (local_hist[tid] != 0) atomicAdd(&hist[tid], local_hist[tid]);
}
void check(cudaError_t e, const char* what) {
    if (e != cudaSuccess) throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(e));
}
}
bool cuda_available() {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}
const char* cuda_name() {
    static std::string name = [] { int d=0; cudaDeviceProp p{}; if(cudaGetDevice(&d)!=cudaSuccess || cudaGetDeviceProperties(&p,d)!=cudaSuccess) return std::string("CUDA device"); return std::string(p.name); }();
    return name.c_str();
}
ByteStats analyze_cuda(const unsigned char* data, size_t size) {
    ByteStats result;
    if (size == 0) { result.min_byte=0; return result; }
    unsigned char* d_data=nullptr; unsigned int* d_hist=nullptr;
    try {
        check(cudaMalloc(reinterpret_cast<void**>(&d_data),size), "cudaMalloc data");
        check(cudaMalloc(reinterpret_cast<void**>(&d_hist),256*sizeof(unsigned int)), "cudaMalloc histogram");
        check(cudaMemcpy(d_data,data,size,cudaMemcpyHostToDevice), "cudaMemcpy input");
        check(cudaMemset(d_hist,0,256*sizeof(unsigned int)), "cudaMemset histogram");
        const int threads=256;
        const size_t blocks=std::min<size_t>((size+threads-1)/threads, 4096);
        histogram_kernel<<<static_cast<unsigned int>(blocks),threads>>>(d_data,size,d_hist);
        check(cudaGetLastError(), "launch histogram kernel");
        check(cudaDeviceSynchronize(), "synchronize histogram kernel");
        unsigned int hist[256]{};
        check(cudaMemcpy(hist,d_hist,sizeof(hist),cudaMemcpyDeviceToHost), "copy histogram");
        double entropy=0.0; const double n=static_cast<double>(size); result.min_byte=255;
        for(size_t i=0;i<256;++i) {
            result.histogram[i]=hist[i];
            result.sum_bytes+=static_cast<uint64_t>(i)*hist[i];
            if(i==0) result.zero_bytes=hist[i]; else if(hist[i]) { if(result.nonzero_bytes==0) result.min_byte=static_cast<uint8_t>(i); result.nonzero_bytes+=hist[i]; result.max_byte=static_cast<uint8_t>(i); }
            if(hist[i]) { const double p=static_cast<double>(hist[i])/n; entropy-=p*std::log2(p); }
        }
        if(result.nonzero_bytes==0) result.min_byte=0;
        result.entropy_bits_per_byte=entropy;
    } catch (...) {
        if(d_data) cudaFree(d_data); if(d_hist) cudaFree(d_hist); throw;
    }
    cudaFree(d_data); cudaFree(d_hist);
    return result;
}
}
