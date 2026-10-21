#include "gguf_engine.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>

static void help() {
    std::cout << "GGUF Analyzer Engine v0.4.0 CPU-only\n"
              << "Usage: gguf_analyzer <input.gguf> [--out report.json] [--chunk-mib N] [--backend cpu] [--no-hash] OR --decode-at OFFSET --decode-type ID --decode-total N [--decode-count N] --decode-out values.json\n"
              << "CPU-only backend. CUDA is not compiled or called.\n";
}
int main(int argc, char** argv) {
    if (argc < 2) { help(); return 2; }
    std::filesystem::path input = argv[1], output = "gguf_report.json";
    gguf::Options options;
    options.backend = "cpu";
    bool decode_mode=false; uint64_t decode_offset=0, decode_total=0, decode_count=256; uint32_t decode_type=0; std::filesystem::path decode_output="decoded_values.json";
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help") { help(); return 0; }
        else if (a == "--out" && i + 1 < argc) output = argv[++i];
        else if (a == "--chunk-mib" && i + 1 < argc) {
            uint64_t n = 0; std::string s = argv[++i];
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
            if (ec != std::errc{} || p != s.data() + s.size() || n == 0 || n > 256) {
                std::cerr << "Invalid --chunk-mib (1..256 expected)\n"; return 2;
            }
            options.chunk_size = n * 1024ull * 1024ull;
        } else if (a == "--backend" && i + 1 < argc) {
            options.backend = argv[++i];
            if (options.backend != "cpu") {
                std::cerr << "This build supports only --backend cpu\n"; return 2;
            }
        } else if (a == "--decode-at" && i+1<argc) { decode_mode=true; decode_offset=std::stoull(argv[++i]); }
        else if (a == "--decode-type" && i+1<argc) decode_type=static_cast<uint32_t>(std::stoul(argv[++i]));
        else if (a == "--decode-total" && i+1<argc) decode_total=std::stoull(argv[++i]);
        else if (a == "--decode-count" && i+1<argc) decode_count=std::stoull(argv[++i]);
        else if (a == "--decode-out" && i+1<argc) decode_output=argv[++i];
        else if (a == "--no-hash") options.calculate_sha256 = false;
        else { std::cerr << "Unknown argument: " << a << "\n"; help(); return 2; }
    }
    try {
        if (decode_mode) {
            auto values=gguf::decode_tensor_values(input, decode_type, decode_offset, decode_total, decode_count);
            std::ofstream out(decode_output, std::ios::binary);
            if(!out) { std::cerr << "Cannot write decoded output: " << decode_output.string() << "\n"; return 3; }
            out << gguf::decode_values_to_json(decode_type, decode_offset, decode_total, values);
            if(!out) { std::cerr << "Failed writing decoded output\n"; return 3; }
            std::cout << "Decoded " << values.size() << " / " << decode_total << " values to " << decode_output.string() << "\n";
            return 0;
        }
        gguf::Engine engine;
        auto report = engine.analyze(input, options);
        report.requested_backend = "cpu";
        std::ofstream out(output, std::ios::binary);
        if (!out) { std::cerr << "Cannot write report: " << output.string() << "\n"; return 3; }
        out << gguf::report_to_json(report); out.close();
        if (!out) { std::cerr << "Failed while writing report: " << output.string() << "\n"; return 3; }
        std::cout << "GGUF Analyzer Engine v0.4.0 CPU-only\n"
                  << "File size: " << report.file_size << " bytes\n"
                  << "Structure: " << report.structure_status << "\n"
                  << "Physical scan: " << report.bytes_scanned << " / " << report.file_size
                  << " bytes (" << report.coverage_percent << "%)\n"
                  << "Backend: cpu\n"
                  << "Scan speed: " << report.scan_mib_per_second << " MiB/s ("
                  << report.scan_seconds << " s)\n"
                  << "Byte entropy: " << report.aggregate_stats.entropy_bits_per_byte << " bits/byte\n"
                  << "Metadata: " << report.metadata.size() << " / " << report.metadata_count_declared << "\n"
                  << "Tensors: " << report.tensors.size() << " / " << report.tensor_count_declared << "\n"
                  << "SHA-256: " << (report.sha256.empty() ? "(not calculated)" : report.sha256) << "\n"
                  << "Issues: " << report.issues.size() << "\n"
                  << "Report: " << output.string() << "\n";
        return report.structure_parse_ok && report.coverage_percent == 100.0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n"; return 10;
    }
}
