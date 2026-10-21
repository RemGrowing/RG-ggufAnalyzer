#include "gguf_engine.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>

static void help() {
    std::cout << "GGUF Analyzer Engine v0.1.0\n"
              << "Usage: gguf_analyzer <input.gguf> [--out report.json] [--chunk-mib N] [--no-hash]\n";
}
int main(int argc, char** argv) {
    if(argc<2) { help(); return 2; }
    std::filesystem::path input;
    std::filesystem::path output="gguf_report.json";
    gguf::Options options;
    input=argv[1];
    for(int i=2;i<argc;++i) {
        std::string a=argv[i];
        if(a=="--help") {help();return 0;}
        else if(a=="--out" && i+1<argc) output=argv[++i];
        else if(a=="--chunk-mib" && i+1<argc) {
            uint64_t n=0; std::string s=argv[++i];
            auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
            if(ec!=std::errc{} || p!=s.data()+s.size() || n==0 || n>4096) {std::cerr<<"Invalid --chunk-mib (1..4096 expected)\n";return 2;}
            options.chunk_size=n*1024ull*1024ull;
        } else if(a=="--no-hash") options.calculate_sha256=false;
        else {std::cerr<<"Unknown argument: "<<a<<"\n";help();return 2;}
    }
    try {
        gguf::Engine engine;
        auto report=engine.analyze(input,options);
        std::ofstream out(output,std::ios::binary);
        if(!out) {std::cerr<<"Cannot write report: "<<output.string()<<"\n";return 3;}
        out<<gguf::report_to_json(report);
        out.close();
        std::cout<<"GGUF Analyzer Engine v0.1.0\n"
                 <<"File size: "<<report.file_size<<" bytes\n"
                 <<"Structure: "<<report.structure_status<<"\n"
                 <<"Physical scan: "<<report.bytes_scanned<<" / "<<report.file_size<<" bytes ("<<report.coverage_percent<<"%)\n"
                 <<"Metadata: "<<report.metadata.size()<<" / "<<report.metadata_count_declared<<"\n"
                 <<"Tensors: "<<report.tensors.size()<<" / "<<report.tensor_count_declared<<"\n"
                 <<"Issues: "<<report.issues.size()<<"\n"
                 <<"Report: "<<output.string()<<"\n";
        return report.structure_parse_ok && report.coverage_percent==100.0 ? 0 : 1;
    } catch(const std::exception& e) { std::cerr<<"Fatal: "<<e.what()<<"\n";return 10; }
}
