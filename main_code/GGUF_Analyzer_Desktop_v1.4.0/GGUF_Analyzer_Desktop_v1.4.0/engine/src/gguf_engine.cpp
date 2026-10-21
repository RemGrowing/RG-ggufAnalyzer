#include "gguf_engine.hpp"
#include "byte_backend.hpp"
#include <chrono>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gguf {
namespace {
class Reader {
public:
    explicit Reader(std::ifstream& f, uint64_t size) : f_(f), size_(size) {}
    uint64_t pos() const { return pos_; }
    uint64_t size() const { return size_; }
    void seek(uint64_t p) {
        if (p > size_) throw std::runtime_error("seek beyond end of file");
        f_.clear(); f_.seekg(static_cast<std::streamoff>(p), std::ios::beg);
        if (!f_) throw std::runtime_error("seek failed");
        pos_ = p;
    }
    void read_bytes(char* dst, uint64_t n) {
        if (n > size_ - std::min(pos_, size_)) throw std::runtime_error("read exceeds file boundary");
        if (n > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::runtime_error("read length exceeds stream limit");
        f_.read(dst, static_cast<std::streamsize>(n));
        if (f_.gcount() != static_cast<std::streamsize>(n)) throw std::runtime_error("short read");
        pos_ += n;
    }
    std::string string_n(uint64_t n) {
        if (n > 64ull * 1024ull * 1024ull) throw std::runtime_error("string length exceeds safety limit (64 MiB)");
        std::string s(static_cast<size_t>(n), '\0');
        if (n) read_bytes(s.data(), n);
        return s;
    }
    template<class T> T le() {
        static_assert(std::is_integral_v<T>);
        std::array<unsigned char, sizeof(T)> b{};
        read_bytes(reinterpret_cast<char*>(b.data()), sizeof(T));
        using U = std::make_unsigned_t<T>;
        U x = 0;
        for (size_t i=0;i<b.size();++i) x |= static_cast<U>(b[i]) << (i*8);
        return static_cast<T>(x);
    }
    float f32() { uint32_t u=le<uint32_t>(); return std::bit_cast<float>(u); }
    double f64() { uint64_t u=le<uint64_t>(); return std::bit_cast<double>(u); }
private:
    std::ifstream& f_; uint64_t size_; uint64_t pos_{0};
};

std::string type_name(uint32_t t) {
    static const std::map<uint32_t,std::string> names = {
      {0,"F32"},{1,"F16"},{2,"Q4_0"},{3,"Q4_1"},{4,"Q4_2_DEPRECATED"},{5,"Q4_3_DEPRECATED"},
      {6,"Q5_0"},{7,"Q5_1"},{8,"Q8_0"},{9,"Q8_1"},{10,"Q2_K"},{11,"Q3_K"},{12,"Q4_K"},
      {13,"Q5_K"},{14,"Q6_K"},{15,"Q8_K"},{16,"IQ2_XXS"},{17,"IQ2_XS"},{18,"IQ3_XXS"},
      {19,"IQ1_S"},{20,"IQ4_NL"},{21,"IQ3_S"},{22,"IQ2_S"},{23,"IQ4_XS"},{24,"I8"},
      {25,"I16"},{26,"I32"},{27,"I64"},{28,"F64"},{29,"IQ1_M"},{30,"BF16"},
      {31,"Q4_0_4_4"},{32,"Q4_0_4_8"},{33,"Q4_0_8_8"},{34,"TQ1_0"},{35,"TQ2_0"},
      {36,"IQ4_NL_4_4"},{37,"IQ4_NL_4_8"},{38,"IQ4_NL_8_8"},{39,"MXFP4"},{40,"NVFP4"}
    };
    auto it=names.find(t); return it==names.end() ? "UNKNOWN_TYPE_"+std::to_string(t) : it->second;
}
std::optional<uint64_t> tensor_size(uint32_t t, uint64_t elements) {
    // (block elements, bytes per block), from common GGML type layouts.
    struct B { uint64_t n, bytes; };
    static const std::map<uint32_t,B> b = {
      {0,{1,4}},{1,{1,2}},{2,{32,18}},{3,{32,20}},{6,{32,22}},{7,{32,24}},
      {8,{32,34}},{9,{32,40}},{10,{256,84}},{11,{256,110}},{12,{256,144}},
      {13,{256,176}},{14,{256,210}},{15,{256,292}},{16,{256,66}},{17,{256,74}},
      {18,{256,98}},{19,{256,56}}
    };
    auto it=b.find(t);
    if (it==b.end()) {
      static const std::map<uint32_t,B> extra = {
        {20,{32,18}},{21,{256,110}},{22,{256,82}},{23,{256,136}},
        {24,{1,1}},{25,{1,2}},{26,{1,4}},{27,{1,8}},{28,{1,8}},{29,{256,56}},{30,{1,2}}
      };
      auto e=extra.find(t); if(e==extra.end()) return std::nullopt;
      if(elements % e->second.n != 0) return std::nullopt;
      if(elements/e->second.n > std::numeric_limits<uint64_t>::max()/e->second.bytes) return std::nullopt;
      return (elements/e->second.n)*e->second.bytes;
    }
    if (elements % it->second.n != 0) return std::nullopt;
    if (elements/it->second.n > std::numeric_limits<uint64_t>::max()/it->second.bytes) return std::nullopt;
    return (elements/it->second.n)*it->second.bytes;
}
std::string quant_profile_name(uint64_t id) {
    // GGUF general.file_type values from the GGML file-type enum.
    static const std::map<uint64_t, std::string> names = {
      {0,"F32"},{1,"F16"},{2,"Q4_0"},{3,"Q4_1"},{4,"Q4_1_SOME_F16"},
      {5,"Q4_2_DEPRECATED"},{6,"Q4_3_DEPRECATED"},{7,"Q8_0"},{8,"Q5_0"},
      {9,"Q5_1"},{10,"Q2_K"},{11,"Q3_K_S"},{12,"Q3_K_M"},{13,"Q3_K_L"},
      {14,"Q4_K_S"},{15,"Q4_K_M"},{16,"Q5_K_S"},{17,"Q5_K_M"},{18,"Q6_K"},
      {19,"IQ2_XXS"},{20,"IQ2_XS"},{21,"IQ3_XXS"},{22,"IQ1_S"},{23,"IQ4_NL"},
      {24,"IQ3_S"},{25,"IQ2_S"},{26,"IQ4_XS"},{27,"IQ1_M"},{28,"BF16"},
      {29,"Q4_0_4_4"},{30,"Q4_0_4_8"},{31,"Q4_0_8_8"},{32,"TQ1_0"},{33,"TQ2_0"}
    };
    auto it = names.find(id);
    return it == names.end() ? "UNKNOWN_FILE_TYPE_" + std::to_string(id) : it->second;
}
std::optional<uint64_t> declared_file_type(const Report& r) {
    for (const auto& m : r.metadata) if (m.key == "general.file_type") {
        try { size_t used=0; auto v=std::stoull(m.value, &used); if (used==m.value.size()) return v; }
        catch (...) {}
    }
    return std::nullopt;
}

std::string meta_type(uint32_t t) {
    static const char* n[]={"UINT8","INT8","UINT16","INT16","UINT32","INT32","FLOAT32","BOOL","STRING","ARRAY","UINT64","INT64","FLOAT64"};
    return t < std::size(n) ? n[t] : "UNKNOWN_META_TYPE_"+std::to_string(t);
}
std::string value_repr(Reader& r, uint32_t type, unsigned depth=0) {
    if(depth>8) throw std::runtime_error("metadata array nesting exceeds safety limit");
    switch(type) {
      case 0: return std::to_string(r.le<uint8_t>());
      case 1: return std::to_string(r.le<int8_t>());
      case 2: return std::to_string(r.le<uint16_t>());
      case 3: return std::to_string(r.le<int16_t>());
      case 4: return std::to_string(r.le<uint32_t>());
      case 5: return std::to_string(r.le<int32_t>());
      case 6: { float v=r.f32(); if(!std::isfinite(v)) return "non_finite"; std::ostringstream s; s<<std::setprecision(9)<<v; return s.str(); }
      case 7: return r.le<uint8_t>() ? "true" : "false";
      case 8: { auto n=r.le<uint64_t>(); return r.string_n(n); }
      case 9: {
        uint32_t et=r.le<uint32_t>(); uint64_t n=r.le<uint64_t>();
        if(n>10000000ull) throw std::runtime_error("metadata array element count exceeds safety limit");
        std::ostringstream s; s<<"[";
        for(uint64_t i=0;i<n;++i) { if(i) s<<", "; s<<value_repr(r,et,depth+1); }
        s<<"]"; return s.str();
      }
      case 10: return std::to_string(r.le<uint64_t>());
      case 11: return std::to_string(r.le<int64_t>());
      case 12: { double v=r.f64(); if(!std::isfinite(v)) return "non_finite"; std::ostringstream s; s<<std::setprecision(17)<<v; return s.str(); }
      default: throw std::runtime_error("unknown GGUF metadata value type "+std::to_string(type));
    }
}
void issue(Report& r, std::string sev, std::string code, std::string msg, std::optional<uint64_t> off={}) {
    r.issues.push_back({std::move(sev),std::move(code),std::move(msg),off});
}
std::string sha256_file(const std::filesystem::path& p) {
    static constexpr uint32_t K[64]={
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    auto rr=[](uint32_t x,int n){return (x>>n)|(x<<(32-n));};
    std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("cannot open file for SHA-256");
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    auto transform=[&](const unsigned char* block) {
      uint32_t w[64]{};
      for(int i=0;i<16;++i) w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|(uint32_t(block[i*4+2])<<8)|block[i*4+3];
      for(int i=16;i<64;++i) {uint32_t s0=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3); uint32_t s1=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1;}
      uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],ff=h[5],g=h[6],hh=h[7];
      for(int i=0;i<64;++i){uint32_t S1=rr(e,6)^rr(e,11)^rr(e,25), ch=(e&ff)^(~e&g), t1=hh+S1+ch+K[i]+w[i]; uint32_t S0=rr(a,2)^rr(a,13)^rr(a,22), maj=(a&b)^(a&c)^(b&c),t2=S0+maj; hh=g;g=ff;ff=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
      h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=ff;h[6]+=g;h[7]+=hh;
    };
    std::array<unsigned char,64> block{}; size_t used=0; uint64_t total=0;
    std::vector<char> input(1024*1024);
    while(f) {
      f.read(input.data(),static_cast<std::streamsize>(input.size()));
      auto got=f.gcount(); if(got<=0) break;
      total+=static_cast<uint64_t>(got);
      size_t pos=0, count=static_cast<size_t>(got);
      while(pos<count) {
        size_t take=std::min(block.size()-used,count-pos);
        std::memcpy(block.data()+used,input.data()+pos,take); used+=take; pos+=take;
        if(used==64) {transform(block.data()); used=0;}
      }
    }
    if(!f.eof() && f.fail()) throw std::runtime_error("error while reading file for SHA-256");
    const uint64_t bit_len=total*8;
    block[used++]=0x80;
    if(used>56) {while(used<64)block[used++]=0;transform(block.data());used=0;}
    while(used<56)block[used++]=0;
    for(int i=7;i>=0;--i)block[used++]=static_cast<unsigned char>(bit_len>>(i*8));
    transform(block.data());
    std::ostringstream o; o<<std::hex<<std::setfill('0'); for(auto v:h)o<<std::setw(8)<<v; return o.str();
}
}

Report Engine::analyze(const std::filesystem::path& path, const Options& opt) {
    Report out; out.file_path=path.string();
    std::error_code ec; out.file_size=std::filesystem::file_size(path,ec);
    if(ec) { issue(out,"error","FILE_STAT_FAILED",ec.message()); out.structure_status="failed"; return out; }
    if(opt.chunk_size==0) { issue(out,"error","INVALID_CHUNK_SIZE","Chunk size must be greater than zero."); out.structure_status="failed"; return out; }

    try {
      std::ifstream f(path,std::ios::binary);
      if(!f) throw std::runtime_error("cannot open input file");
      Reader r(f,out.file_size);
      if(out.file_size<24) throw std::runtime_error("file is too small to contain a GGUF header");
      char magic[4]{}; r.read_bytes(magic,4);
      if(std::memcmp(magic,"GGUF",4)!=0) throw std::runtime_error("invalid magic; expected GGUF");
      out.version=r.le<uint32_t>();
      if(out.version<2 || out.version>3) throw std::runtime_error("unsupported GGUF version "+std::to_string(out.version)+" (this engine supports v2/v3)");
      out.tensor_count_declared=r.le<uint64_t>();
      out.metadata_count_declared=r.le<uint64_t>();
      if(out.tensor_count_declared>10000000ull || out.metadata_count_declared>1000000ull)
          throw std::runtime_error("declared item count exceeds safety limit");
      for(uint64_t i=0;i<out.metadata_count_declared;++i) {
        uint64_t keyoff=r.pos();
        uint64_t klen=r.le<uint64_t>(); std::string key=r.string_n(klen);
        uint32_t type=r.le<uint32_t>(); uint64_t valoff=r.pos();
        std::string value=value_repr(r,type);
        out.metadata.push_back({std::move(key),type,meta_type(type),std::move(value),valoff});
        (void)keyoff;
      }
      for(uint64_t i=0;i<out.tensor_count_declared;++i) {
        Tensor t;
        uint64_t nlen=r.le<uint64_t>(); t.name=r.string_n(nlen);
        uint32_t nd=r.le<uint32_t>();
        if(nd==0 || nd>4) throw std::runtime_error("invalid tensor dimension count for "+t.name);
        t.element_count=1;
        for(uint32_t d=0;d<nd;++d) {
          uint64_t dim=r.le<uint64_t>(); t.dimensions.push_back(dim);
          if(dim==0 || t.element_count>std::numeric_limits<uint64_t>::max()/dim) throw std::runtime_error("invalid/overflowing tensor shape for "+t.name);
          t.element_count*=dim;
        }
        t.type_id=r.le<uint32_t>(); t.type_name=type_name(t.type_id);
        t.relative_offset=r.le<uint64_t>();
        t.expected_size=tensor_size(t.type_id,t.element_count);
        out.tensors.push_back(std::move(t));
      }
      // Resolve alignment and data start.
      for(const auto& m:out.metadata) if(m.key=="general.alignment") {
        try { auto v=std::stoull(m.value); if(v>0 && v<=4096 && (v&(v-1))==0) out.alignment=v; else issue(out,"warning","INVALID_ALIGNMENT","general.alignment is not a valid power of two; default 32 retained."); }
        catch(...) { issue(out,"warning","INVALID_ALIGNMENT","Could not parse general.alignment; default 32 retained."); }
      }
      uint64_t p=r.pos();
      uint64_t rem=p%out.alignment;
      out.tensor_data_start = rem==0 ? p : p+(out.alignment-rem);
      if(out.tensor_data_start>out.file_size) throw std::runtime_error("tensor data start exceeds file size");
      for(auto& t:out.tensors) {
        if(t.relative_offset>std::numeric_limits<uint64_t>::max()-out.tensor_data_start) {
          issue(out,"error","OFFSET_OVERFLOW","Tensor offset overflows: "+t.name); continue;
        }
        t.absolute_offset=out.tensor_data_start+t.relative_offset;
        if(t.expected_size && t.absolute_offset<=out.file_size && *t.expected_size<=out.file_size-t.absolute_offset)
          t.end_offset=t.absolute_offset+*t.expected_size;
        else if(t.expected_size) issue(out,"error","TENSOR_OUT_OF_BOUNDS","Tensor range exceeds file: "+t.name,t.absolute_offset);
        else issue(out,"warning","UNSUPPORTED_TENSOR_SIZE","Cannot safely calculate tensor size for type "+t.type_name+" ("+t.name+")",t.absolute_offset);
      }
      std::set<std::string> names;
      for(const auto& t:out.tensors) if(!names.insert(t.name).second) issue(out,"warning","DUPLICATE_TENSOR_NAME","Duplicate tensor name: "+t.name);
      std::vector<const Tensor*> ranges;
      for(const auto& t:out.tensors) if(t.end_offset) ranges.push_back(&t);
      std::sort(ranges.begin(),ranges.end(),[](auto a,auto b){return a->absolute_offset<b->absolute_offset;});
      uint64_t last=out.tensor_data_start;
      for(auto t:ranges) {
        if(t->absolute_offset<last) issue(out,"warning","OVERLAPPING_TENSOR_RANGES","Tensor range overlaps previous range: "+t->name,t->absolute_offset);
        if(t->absolute_offset>last) out.gaps.push_back({last,t->absolute_offset,"unclaimed_or_padding"});
        last=std::max(last,*t->end_offset);
      }
      if(last<out.file_size) out.gaps.push_back({last,out.file_size,"unclaimed_or_trailing"});
      out.structure_parse_ok=true; out.structure_status="parsed_with_checks";
    } catch(const std::exception& e) {
      issue(out,"error","STRUCTURE_PARSE_FAILED",e.what());
      out.structure_status="failed";
    }

    // Independent full-file scan. CPU-only build: CUDA is neither queried nor invoked.
    try {
      std::ifstream f(path,std::ios::binary);
      if(!f) throw std::runtime_error("cannot reopen file for physical scan");
      const auto scan_start=std::chrono::steady_clock::now();
      out.gpu_available=false;
      out.gpu_device.clear();
      out.gpu_chunks_processed=0;
      out.active_backend="cpu";
      std::vector<char> buffer(static_cast<size_t>(std::min<uint64_t>(opt.chunk_size, 256ull*1024ull*1024ull)));
      if(buffer.empty()) throw std::runtime_error("scan buffer size is zero");
      uint64_t off=0;
      while(off<out.file_size) {
        uint64_t n=std::min<uint64_t>(buffer.size(),out.file_size-off);
        f.clear(); f.seekg(static_cast<std::streamoff>(off),std::ios::beg);
        f.read(buffer.data(),static_cast<std::streamsize>(n));
        auto got=static_cast<uint64_t>(f.gcount());
        ScanChunk c{off,got,got==n,got==n ? "" : "short read or I/O error",{}};
        if(got>0) {
          c.stats=backend::analyze_cpu(reinterpret_cast<const unsigned char*>(buffer.data()),static_cast<size_t>(got));
          auto& a=out.aggregate_stats;
          a.zero_bytes+=c.stats.zero_bytes; a.nonzero_bytes+=c.stats.nonzero_bytes; a.sum_bytes+=c.stats.sum_bytes;
          if(c.stats.min_byte<a.min_byte) a.min_byte=c.stats.min_byte;
          if(c.stats.max_byte>a.max_byte) a.max_byte=c.stats.max_byte;
          for(size_t i=0;i<256;++i) a.histogram[i]+=c.stats.histogram[i];
        }
        out.chunks.push_back(c); out.bytes_scanned+=got; off+=got;
        if(got!=n) { issue(out,"error","CHUNK_READ_FAILED","Could not read complete chunk",c.offset); break; }
      }
      double entropy=0.0;
      if(out.bytes_scanned>0) for(const auto count:out.aggregate_stats.histogram) if(count) { const double p=static_cast<double>(count)/static_cast<double>(out.bytes_scanned); entropy-=p*std::log2(p); }
      out.aggregate_stats.entropy_bits_per_byte=entropy;
      if(out.bytes_scanned==0) out.aggregate_stats.min_byte=0;
      out.coverage_percent=out.file_size==0?100.0:(100.0*static_cast<double>(out.bytes_scanned)/static_cast<double>(out.file_size));
      out.scan_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-scan_start).count();
      out.scan_mib_per_second=out.scan_seconds>0 ? (static_cast<double>(out.bytes_scanned)/(1024.0*1024.0))/out.scan_seconds : 0.0;
    } catch(const std::exception& e) { issue(out,"error","PHYSICAL_SCAN_FAILED",e.what()); }

    if(opt.calculate_sha256) {
      try { out.sha256=sha256_file(path); }
      catch(const std::exception& e) { issue(out,"warning","SHA256_FAILED",e.what()); }
    }
    return out;
}

namespace {
uint16_t read_u16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (uint16_t(p[1]) << 8)); }
float half_to_float(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1fu, mant = h & 0x3ffu, bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else { int e = -14; while ((mant & 0x400u) == 0) { mant <<= 1; --e; } mant &= 0x3ffu; bits = sign | (uint32_t(e + 127) << 23) | (mant << 13); }
    } else if (exp == 31) bits = sign | 0x7f800000u | (mant << 13);
    else bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    return std::bit_cast<float>(bits);
}
}
std::vector<float> decode_tensor_values(const std::filesystem::path& path, uint32_t type, uint64_t offset, uint64_t total, uint64_t max_elements) {
    if (max_elements == 0 || max_elements > 1000000) throw std::runtime_error("decode count must be in 1..1000000");
    const uint64_t count = std::min(total, max_elements);
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open GGUF file");
    f.seekg(0, std::ios::end); const auto end = f.tellg();
    if (end < 0 || offset > static_cast<uint64_t>(end)) throw std::runtime_error("tensor offset is outside the file");
    auto read_at = [&](uint64_t at, size_t n) {
        if (at > static_cast<uint64_t>(end) || n > static_cast<uint64_t>(end) - at) throw std::runtime_error("tensor data read exceeds file boundary");
        std::vector<unsigned char> b(n); f.clear(); f.seekg(static_cast<std::streamoff>(at), std::ios::beg); f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n));
        if (f.gcount() != static_cast<std::streamsize>(n)) throw std::runtime_error("short tensor read");
        return b;
    };
    std::vector<float> out; out.reserve(static_cast<size_t>(count));
    auto append = [&](float x) { if (out.size() < count) out.push_back(x); };
    if (type == 0 || type == 1 || type == 24 || type == 25 || type == 26 || type == 27 || type == 28 || type == 30) {
        const size_t width = (type==0||type==26)?4:(type==1||type==25||type==30)?2:(type==24)?1:8;
        auto b=read_at(offset, static_cast<size_t>(count)*width);
        for(uint64_t i=0;i<count;++i) { const auto* p=b.data()+i*width; float v=0;
            if(type==0) { uint32_t u=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); v=std::bit_cast<float>(u); }
            else if(type==1) v=half_to_float(read_u16(p));
            else if(type==24) v=static_cast<int8_t>(p[0]);
            else if(type==25) v=static_cast<int16_t>(read_u16(p));
            else if(type==26) { uint32_t u=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); v=static_cast<int32_t>(u); }
            else if(type==27) { uint64_t u=0; for(int k=0;k<8;++k)u|=uint64_t(p[k])<<(8*k); v=static_cast<float>(static_cast<int64_t>(u)); }
            else if(type==28) { uint64_t u=0; for(int k=0;k<8;++k)u|=uint64_t(p[k])<<(8*k); v=static_cast<float>(std::bit_cast<double>(u)); }
            else { uint16_t u=read_u16(p); v=half_to_float(u); if(type==30) { uint32_t bits=uint32_t(u)<<16; v=std::bit_cast<float>(bits); } }
            append(v);
        }
        return out;
    }
    uint64_t block_size=0, bytes=0;
    switch(type) {
        case 2:block_size=32;bytes=18;break; case 3:block_size=32;bytes=20;break;
        case 6:block_size=32;bytes=22;break; case 7:block_size=32;bytes=24;break;
        case 8:block_size=32;bytes=34;break;
        case 11:block_size=256;bytes=110;break; // Q3_K
        case 12:block_size=256;bytes=144;break; // Q4_K
        case 13:block_size=256;bytes=176;break; // Q5_K
        case 14:block_size=256;bytes=210;break; // Q6_K
        default: throw std::runtime_error("decoder supports F32/F16/BF16, integer scalar types, Q4_0/Q4_1/Q5_0/Q5_1/Q8_0, Q3_K/Q4_K/Q5_K/Q6_K; this tensor type is not yet implemented");
    }
    if(total % block_size != 0) throw std::runtime_error("tensor element count is not divisible by quantization block size");
    const uint64_t blocks=(count+block_size-1)/block_size;
    if(blocks > std::numeric_limits<size_t>::max()/bytes) throw std::runtime_error("decode buffer size overflow");
    auto b=read_at(offset,static_cast<size_t>(blocks*bytes));
    out.assign(static_cast<size_t>(count), 0.0f);
    for(uint64_t bi=0;bi<blocks;++bi) {
        const unsigned char* p=b.data()+bi*bytes; const float d=half_to_float(read_u16(p));
        if(type==2 || type==3) {
            const float m=type==3?half_to_float(read_u16(p+2)):0.0f; const unsigned char* qs=p+(type==3?4:2);
            for(int j=0;j<16;++j) { const uint64_t i0=bi*32+j, i1=bi*32+j+16; const float v0=(float(qs[j]&15)-(type==2?8.0f:0.0f))*d+m; const float v1=(float(qs[j]>>4)-(type==2?8.0f:0.0f))*d+m; if(i0<count) out[static_cast<size_t>(i0)]=v0; if(i1<count) out[static_cast<size_t>(i1)]=v1; }
        } else if(type==6 || type==7) {
            const float m=type==7?half_to_float(read_u16(p+2)):0.0f; const uint32_t qh=uint32_t(p[type==7?4:2])|(uint32_t(p[type==7?5:3])<<8)|(uint32_t(p[type==7?6:4])<<16)|(uint32_t(p[type==7?7:5])<<24); const unsigned char* qs=p+(type==7?8:6);
            for(int j=0;j<16;++j) { int lo=(qs[j]&15)|(((qh>>j)&1)<<4), hi=(qs[j]>>4)|(((qh>>(j+16))&1)<<4); const uint64_t i0=bi*32+j, i1=bi*32+j+16; if(i0<count) out[static_cast<size_t>(i0)]=(float(lo)-(type==6?16.0f:0.0f))*d+m; if(i1<count) out[static_cast<size_t>(i1)]=(float(hi)-(type==6?16.0f:0.0f))*d+m; }
        } else if (type==8) {
            const signed char* qs=reinterpret_cast<const signed char*>(p+2);
            for(int j=0;j<32;++j) { const uint64_t idx=bi*32+j; if(idx<count) out[static_cast<size_t>(idx)]=d*qs[j]; }
        } else if (type==12) { // Q4_K: 256 values, 144 bytes per super-block
            const float dmin=half_to_float(read_u16(p+2));
            const unsigned char* scales=p+4; const unsigned char* qs=p+16;
            auto scale_min = [&](int j, uint8_t& sc, uint8_t& mn) {
                if (j<4) { sc=scales[j]&63; mn=scales[j+4]&63; }
                else { sc=(scales[j+4]&15)|((scales[j-4]>>6)<<4); mn=(scales[j+4]>>4)|((scales[j]>>6)<<4); }
            };
            for(int j=0;j<256;j+=64) {
                uint8_t sc0=0,mn0=0,sc1=0,mn1=0;
                scale_min(j/32,sc0,mn0); scale_min(j/32+1,sc1,mn1);
                for(int l=0;l<32;++l) {
                    uint64_t i0=bi*256+j+l, i1=bi*256+j+l+32;
                    float v0=d*sc0*float(qs[l]&15)-dmin*mn0;
                    float v1=d*sc1*float(qs[l]>>4)-dmin*mn1;
                    if(i0<count) out[static_cast<size_t>(i0)]=v0;
                    if(i1<count) out[static_cast<size_t>(i1)]=v1;
                }
                qs+=32;
            }
        } else if (type==13) { // Q5_K: 256 values, 176 bytes per super-block
            const float dmin=half_to_float(read_u16(p+2));
            const unsigned char* scales=p+4;
            const unsigned char* qh=p+16;
            const unsigned char* qs=p+48;
            auto scale_min = [&](int j, uint8_t& sc, uint8_t& mn) {
                if (j<4) { sc=scales[j]&63; mn=scales[j+4]&63; }
                else { sc=(scales[j+4]&15)|((scales[j-4]>>6)<<4); mn=(scales[j+4]>>4)|((scales[j]>>6)<<4); }
            };
            for(int j=0;j<256;j+=64) {
                uint8_t sc0=0,mn0=0,sc1=0,mn1=0;
                scale_min(j/32,sc0,mn0); scale_min(j/32+1,sc1,mn1);
                const uint8_t mask0=static_cast<uint8_t>(1u<<(j/32));
                const uint8_t mask1=static_cast<uint8_t>(1u<<(j/32+1));
                const unsigned char* q=qs+j/2;
                for(int l=0;l<32;++l) {
                    const int q0=(q[l]&15)+((qh[l]&mask0)?16:0);
                    const int q1=(q[l]>>4)+((qh[l]&mask1)?16:0);
                    const uint64_t i0=bi*256+j+l, i1=bi*256+j+l+32;
                    const float v0=d*sc0*float(q0)-dmin*mn0;
                    const float v1=d*sc1*float(q1)-dmin*mn1;
                    if(i0<count) out[static_cast<size_t>(i0)]=v0;
                    if(i1<count) out[static_cast<size_t>(i1)]=v1;
                }
            }
        } else if (type==14) { // Q6_K: 256 values, 210 bytes per super-block
            const unsigned char* ql=p;
            const unsigned char* qh=p+128;
            const auto* scales=reinterpret_cast<const int8_t*>(p+192);
            const float d6=half_to_float(read_u16(p+208));
            for(int n=0;n<256;n+=128) {
                for(int l=0;l<32;++l) {
                    const int is=l/16;
                    const int q1=(ql[l]&15)|(((qh[l]>>0)&3)<<4);
                    const int q2=(ql[l+32]&15)|(((qh[l]>>2)&3)<<4);
                    const int q3=(ql[l]>>4)|(((qh[l]>>4)&3)<<4);
                    const int q4=(ql[l+32]>>4)|(((qh[l]>>6)&3)<<4);
                    const uint64_t base=bi*256+n;
                    const uint64_t i0=base+l, i1=base+l+32, i2=base+l+64, i3=base+l+96;
                    if(i0<count) out[static_cast<size_t>(i0)]=d6*float(scales[is+0])*(q1-32);
                    if(i1<count) out[static_cast<size_t>(i1)]=d6*float(scales[is+2])*(q2-32);
                    if(i2<count) out[static_cast<size_t>(i2)]=d6*float(scales[is+4])*(q3-32);
                    if(i3<count) out[static_cast<size_t>(i3)]=d6*float(scales[is+6])*(q4-32);
                }
                ql+=64; qh+=32; scales+=8;
            }
        } else if (type==11) { // Q3_K: 256 values, 110 bytes per super-block
            const float d3=half_to_float(read_u16(p+108));
            const unsigned char* hmask=p; // bytes 0..31
            const unsigned char* qs=p+32; // bytes 32..95
            const unsigned char* packed=p+96; // 12 packed 6-bit scales
            uint32_t aux[4]{}; std::memcpy(aux,packed,12);
            const uint32_t kmask1=0x03030303u, kmask2=0x0f0f0f0fu;
            uint32_t tmp=aux[2];
            aux[2]=((aux[0]>>4)&kmask2)|(((tmp>>4)&kmask1)<<4);
            aux[3]=((aux[1]>>4)&kmask2)|(((tmp>>6)&kmask1)<<4);
            aux[0]=(aux[0]&kmask2)|(((tmp>>0)&kmask1)<<4);
            aux[1]=(aux[1]&kmask2)|(((tmp>>2)&kmask1)<<4);
            const auto* scales=reinterpret_cast<const int8_t*>(aux);
            uint8_t mask=1; int is=0;
            for(int n=0;n<256;n+=128) {
                int shift=0;
                for(int j=0;j<4;++j) {
                    float dl=d3*(int(scales[is++])-32);
                    for(int l=0;l<16;++l) { uint64_t idx=bi*256+n+j*32+l; int q=(qs[l]>>shift)&3; q-=((hmask[l]&mask)?0:4); if(idx<count) out[static_cast<size_t>(idx)]=dl*q; }
                    dl=d3*(int(scales[is++])-32);
                    for(int l=0;l<16;++l) { uint64_t idx=bi*256+n+j*32+l+16; int q=(qs[l+16]>>shift)&3; q-=((hmask[l+16]&mask)?0:4); if(idx<count) out[static_cast<size_t>(idx)]=dl*q; }
                    shift+=2; mask<<=1;
                }
                qs+=32;
            }
        }
    }
    return out;
}
std::string decode_values_to_json(uint32_t type, uint64_t offset, uint64_t total, const std::vector<float>& values) {
    std::ostringstream o; o<<"{\"type_id\":"<<type<<",\"offset\":"<<offset<<",\"total_elements\":"<<total<<",\"decoded_elements\":"<<values.size()<<",\"values\":[";
    for(size_t i=0;i<values.size();++i) { if(i)o<<","; if(std::isfinite(values[i])) o<<std::setprecision(9)<<values[i]; else o<<"null"; } o<<"]}\n"; return o.str();
}

std::string json_escape(const std::string& s) {
    std::ostringstream o;
    for(unsigned char c:s) {
      switch(c) { case '"':o<<"\\\"";break; case '\\':o<<"\\\\";break; case '\b':o<<"\\b";break; case '\f':o<<"\\f";break; case '\n':o<<"\\n";break; case '\r':o<<"\\r";break; case '\t':o<<"\\t";break;
        default: if(c<0x20)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(c)<<std::dec; else o<<static_cast<char>(c); }
    } return o.str();
}
std::string report_to_json(const Report& r) {
    std::ostringstream o;
    auto q=[&](const std::string& s){return "\""+json_escape(s)+"\"";};
    o<<"{\n  \"engine_version\": \"0.4.0\",\n  \"file_path\": "<<q(r.file_path)<<",\n  \"file_size\": "<<r.file_size<<",\n  \"sha256\": "<<(r.sha256.empty()?"null":q(r.sha256))<<",\n";
    o<<"  \"gguf\": {\"version\": "<<r.version<<", \"declared_tensor_count\": "<<r.tensor_count_declared<<", \"parsed_tensor_count\": "<<r.tensors.size()<<", \"declared_metadata_count\": "<<r.metadata_count_declared<<", \"parsed_metadata_count\": "<<r.metadata.size()<<", \"alignment\": "<<r.alignment<<", \"tensor_data_start\": "<<r.tensor_data_start<<"},\n";
    o<<"  \"quantization\": {";
    auto ft = declared_file_type(r);
    o<<"\"declared_file_type_id\":";
    if (ft) o<<*ft; else o<<"null";
    o<<",\"declared_profile\":";
    if (ft) o<<q(quant_profile_name(*ft)); else o<<"null";
    o<<",\"tensor_type_counts\":{";
    std::map<std::string,uint64_t> type_counts;
    for (const auto& t : r.tensors) ++type_counts[t.type_name];
    bool first_type=true;
    for (const auto& kv : type_counts) { if(!first_type)o<<","; first_type=false; o<<q(kv.first)<<":"<<kv.second; }
    o<<"}},\n";
    o<<"  \"structure_status\": "<<q(r.structure_status)<<", \"tensor_value_decode_status\": "<<q(r.tensor_value_decode_status)<<",\n";
    o<<"  \"backend\": {\"requested\": "<<q(r.requested_backend)<<", \"active\": "<<q(r.active_backend)<<", \"cuda_available\": "<<(r.gpu_available?"true":"false")<<", \"gpu_device\": "<<(r.gpu_device.empty()?"null":q(r.gpu_device))<<", \"gpu_chunks_processed\": "<<r.gpu_chunks_processed<<"},\n";
    o<<"  \"byte_statistics\": {\"zero_bytes\": "<<r.aggregate_stats.zero_bytes<<", \"nonzero_bytes\": "<<r.aggregate_stats.nonzero_bytes<<", \"sum_bytes\": "<<r.aggregate_stats.sum_bytes<<", \"min_byte\": "<<static_cast<unsigned>(r.aggregate_stats.min_byte)<<", \"max_byte\": "<<static_cast<unsigned>(r.aggregate_stats.max_byte)<<", \"entropy_bits_per_byte\": "<<std::fixed<<std::setprecision(6)<<r.aggregate_stats.entropy_bits_per_byte<<", \"histogram\": [";
    for(size_t i=0;i<256;++i){if(i)o<<",";o<<r.aggregate_stats.histogram[i];}
    o<<"]},\n";
    o<<"  \"performance\": {\"scan_seconds\": "<<std::fixed<<std::setprecision(6)<<r.scan_seconds<<", \"scan_mib_per_second\": "<<r.scan_mib_per_second<<"},\n";
    o<<"  \"physical_scan\": {\"bytes_scanned\": "<<r.bytes_scanned<<", \"coverage_percent\": "<<std::fixed<<std::setprecision(6)<<r.coverage_percent<<", \"chunks\": [";
    for(size_t i=0;i<r.chunks.size();++i){auto& c=r.chunks[i]; if(i)o<<",";o<<"{\"offset\":"<<c.offset<<",\"length\":"<<c.length<<",\"success\":"<<(c.success?"true":"false")<<",\"error\":"<<q(c.error)<<",\"zero_bytes\":"<<c.stats.zero_bytes<<",\"nonzero_bytes\":"<<c.stats.nonzero_bytes<<",\"entropy_bits_per_byte\":"<<c.stats.entropy_bits_per_byte<<"}";}
    o<<"]},\n  \"metadata\": [";
    for(size_t i=0;i<r.metadata.size();++i){auto& m=r.metadata[i];if(i)o<<",";o<<"{\"key\":"<<q(m.key)<<",\"type_id\":"<<m.type_id<<",\"type\":"<<q(m.type_name)<<",\"value\":"<<q(m.value)<<",\"value_offset\":"<<m.value_offset<<"}";}
    o<<"],\n  \"tensors\": [";
    for(size_t i=0;i<r.tensors.size();++i){auto& t=r.tensors[i];if(i)o<<",";o<<"{\"name\":"<<q(t.name)<<",\"dimensions\":[";
      for(size_t j=0;j<t.dimensions.size();++j){if(j)o<<",";o<<t.dimensions[j];}
      o<<"],\"element_count\":"<<t.element_count<<",\"type_id\":"<<t.type_id<<",\"type\":"<<q(t.type_name)<<",\"relative_offset\":"<<t.relative_offset<<",\"absolute_offset\":"<<t.absolute_offset<<",\"expected_size\":";
      if(t.expected_size)o<<*t.expected_size;else o<<"null";o<<",\"end_offset\":";if(t.end_offset)o<<*t.end_offset;else o<<"null";o<<"}";}
    o<<"],\n  \"unparsed_gaps\": [";
    for(size_t i=0;i<r.gaps.size();++i){auto& g=r.gaps[i];if(i)o<<",";o<<"{\"start\":"<<g.start<<",\"end\":"<<g.end<<",\"length\":"<<(g.end-g.start)<<",\"classification\":"<<q(g.classification)<<"}";}
    o<<"],\n  \"issues\": [";
    for(size_t i=0;i<r.issues.size();++i){auto& x=r.issues[i];if(i)o<<",";o<<"{\"severity\":"<<q(x.severity)<<",\"code\":"<<q(x.code)<<",\"message\":"<<q(x.message)<<",\"offset\":";if(x.offset)o<<*x.offset;else o<<"null";o<<"}";}
    o<<"]\n}\n"; return o.str();
}
}
