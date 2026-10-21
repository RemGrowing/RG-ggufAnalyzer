#include "gguf_engine.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>

static void help() {
    std::cout << "GGUF Analyzer Engine v0.2.0\n"
              << "Usage: gguf_analyzer <input.gguf> [--out report.json] [--chunk-mib N] [--backend auto|cpu|cuda] [--no-hash]\n"
              << "  auto: use CUDA when a compatible device is available, otherwise CPU\n"
              << "  cpu:  force CPU byte-statistics backend\n"
              << "  cuda: request CUDA; fall back to CPU if unavailable or a chunk fails\n";
}
int main(int argc, char** argv) {
    if(argc<2) { help(); return 2; }
    std::filesystem::path input=argv[1], output="gguf_report.json";
    gguf::Options options;
    for(int i=2;i<argc;++i) {
        std::string a=argv[i];
        if(a=="--help") {help();return 0;}
        else if(a=="--out" && i+1<argc) output=argv[++i];
        else if(a=="--chunk-mib" && i+1<argc) {
            uint64_t n=0; std::string s=argv[++i];
            auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
            if(ec!=std::errc{} || p!=s.data()+s.size() || n==0 || n>256) {std::cerr<<"Invalid --chunk-mib (1..256 expected)\n";return 2;}
            options.chunk_size=n*1024ull*1024ull;
        } else if(a=="--backend" && i+1<argc) {
            options.backend=argv[++i];
            if(options.backend!="auto" && options.backend!="cpu" && options.backend!="cuda") {std::cerr<<"Invalid backend: use auto, cpu, or cuda\n";return 2;}
        } else if(a=="--no-hash") options.calculate_sha256=false;
        else {std::cerr<<"Unknown argument: "<<a<<"\n";help();return 2;}
    }
    try {
        gguf::Engine engine;
        auto report=engine.analyze(input,options);
        report.requested_backend=options.backend;
        std::ofstream out(output,std::ios::binary);
        if(!out) {std::cerr<<"Cannot write report: "<<output.string()<<"\n";return 3;}
        out<<gguf::report_to_json(report); out.close();
        std::cout<<"GGUF Analyzer Engine v0.2.0\n"
                 <<"File size: "<<report.file_size<<" bytes\n"
                 <<"Structure: "<<report.structure_status<<"\n"
                 <<"Physical scan: "<<report.bytes_scanned<<" / "<<report.file_size<<" bytes ("<<report.coverage_percent<<"%)\n"
                 <<"Backend: "<<report.active_backend<<" (CUDA available: "<<(report.gpu_available?"yes":"no")<<")\n"
                 <<"GPU chunks: "<<report.gpu_chunks_processed<<"\n"
                 <<"Scan speed: "<<report.scan_mib_per_second<<" MiB/s ("<<report.scan_seconds<<" s)\n"
                 <<"Byte entropy: "<<report.aggregate_stats.entropy_bits_per_byte<<" bits/byte\n"
                 <<"Metadata: "<<report.metadata.size()<<" / "<<report.metadata_count_declared<<"\n"
                 <<"Tensors: "<<report.tensors.size()<<" / "<<report.tensor_count_declared<<"\n"
                 <<"Issues: "<<report.issues.size()<<"\nReport: "<<output.string()<<"\n";
        return report.structure_parse_ok && report.coverage_percent==100.0 ? 0 : 1;
    } catch(const std::exception& e) { std::cerr<<"Fatal: "<<e.what()<<"\n";return 10; }
}
