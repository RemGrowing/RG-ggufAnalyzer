#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace gguf {
struct Issue {
    std::string severity;
    std::string code;
    std::string message;
    std::optional<uint64_t> offset;
};
struct Metadata {
    std::string key;
    uint32_t type_id{};
    std::string type_name;
    std::string value;
    uint64_t value_offset{};
};
struct Tensor {
    std::string name;
    std::vector<uint64_t> dimensions;
    uint32_t type_id{};
    std::string type_name;
    uint64_t relative_offset{};
    uint64_t absolute_offset{};
    uint64_t element_count{};
    std::optional<uint64_t> expected_size;
    std::optional<uint64_t> end_offset;
};
struct ByteStats {
    uint64_t zero_bytes{};
    uint64_t nonzero_bytes{};
    uint64_t sum_bytes{};
    uint8_t min_byte{255};
    uint8_t max_byte{};
    std::array<uint64_t, 256> histogram{};
    double entropy_bits_per_byte{};
};
struct ScanChunk {
    uint64_t offset{};
    uint64_t length{};
    bool success{};
    std::string error;
    ByteStats stats;
};
struct Gap {
    uint64_t start{};
    uint64_t end{};
    std::string classification;
};
struct Report {
    std::string file_path;
    uint64_t file_size{};
    uint32_t version{};
    uint64_t tensor_count_declared{};
    uint64_t metadata_count_declared{};
    uint64_t alignment{32};
    uint64_t tensor_data_start{};
    bool structure_parse_ok{false};
    std::string structure_status{"not_started"};
    std::string tensor_value_decode_status{"partial_supported"};
    std::string sha256;
    uint64_t bytes_scanned{};
    double coverage_percent{};
    std::string requested_backend{"auto"};
    std::string active_backend{"cpu"};
    bool gpu_available{false};
    std::string gpu_device;
    uint64_t gpu_chunks_processed{};
    double scan_seconds{};
    double scan_mib_per_second{};
    ByteStats aggregate_stats;
    std::vector<Metadata> metadata;
    std::vector<Tensor> tensors;
    std::vector<ScanChunk> chunks;
    std::vector<Gap> gaps;
    std::vector<Issue> issues;
};

struct Options {
    uint64_t chunk_size{64ull * 1024ull * 1024ull};
    bool calculate_sha256{true};
    std::string backend{"auto"}; // auto, cpu, cuda
};

class Engine {
public:
    Report analyze(const std::filesystem::path& path, const Options& options);
};

std::string json_escape(const std::string& s);
std::string report_to_json(const Report& report);
std::vector<float> decode_tensor_values(const std::filesystem::path& path, uint32_t type_id, uint64_t absolute_offset, uint64_t total_elements, uint64_t max_elements);
std::string decode_values_to_json(uint32_t type_id, uint64_t offset, uint64_t total_elements, const std::vector<float>& values);
}
