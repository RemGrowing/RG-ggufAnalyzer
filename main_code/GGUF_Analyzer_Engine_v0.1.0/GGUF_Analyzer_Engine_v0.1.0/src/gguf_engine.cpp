#include "gguf_engine.hpp"
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
    // Compact SHA-256 implementation.
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
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),{});
    uint64_t bits=static_cast<uint64_t>(data.size())*8;
    data.push_back(0x80); while(data.size()%64!=56) data.push_back(0);
    for(int i=7;i>=0;--i) data.push_back(static_cast<unsigned char>(bits>>(i*8)));
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(size_t off=0;off<data.size();off+=64) {
      uint32_t w[64]{};
      for(int i=0;i<16;++i) w[i]=(uint32_t(data[off+i*4])<<24)|(uint32_t(data[off+i*4+1])<<16)|(uint32_t(data[off+i*4+2])<<8)|data[off+i*4+3];
      for(int i=16;i<64;++i) {uint32_t s0=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3); uint32_t s1=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1;}
      uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],ff=h[5],g=h[6],hh=h[7];
      for(int i=0;i<64;++i){uint32_t S1=rr(e,6)^rr(e,11)^rr(e,25), ch=(e&ff)^(~e&g), t1=hh+S1+ch+K[i]+w[i]; uint32_t S0=rr(a,2)^rr(a,13)^rr(a,22), maj=(a&b)^(a&c)^(b&c),t2=S0+maj; hh=g;g=ff;ff=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
      h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=ff;h[6]+=g;h[7]+=hh;
    }
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

    // Independent full-file physical scan. All ranges are recorded, including failures.
    try {
      std::ifstream f(path,std::ios::binary);
      if(!f) throw std::runtime_error("cannot reopen file for physical scan");
      std::vector<char> buffer(static_cast<size_t>(std::min<uint64_t>(opt.chunk_size, 256ull*1024ull*1024ull)));
      uint64_t off=0;
      while(off<out.file_size) {
        uint64_t n=std::min<uint64_t>(buffer.size(),out.file_size-off);
        f.clear(); f.seekg(static_cast<std::streamoff>(off),std::ios::beg);
        f.read(buffer.data(),static_cast<std::streamsize>(n));
        auto got=static_cast<uint64_t>(f.gcount());
        ScanChunk c{off,got,got==n,got==n ? "" : "short read or I/O error"};
        out.chunks.push_back(c); out.bytes_scanned+=got; off+=got;
        if(got!=n) { issue(out,"error","CHUNK_READ_FAILED","Could not read complete chunk",c.offset); break; }
      }
      out.coverage_percent=out.file_size==0?100.0:(100.0*static_cast<double>(out.bytes_scanned)/static_cast<double>(out.file_size));
    } catch(const std::exception& e) { issue(out,"error","PHYSICAL_SCAN_FAILED",e.what()); }

    if(opt.calculate_sha256) {
      try { out.sha256=sha256_file(path); }
      catch(const std::exception& e) { issue(out,"warning","SHA256_FAILED",e.what()); }
    }
    return out;
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
    o<<"{\n  \"engine_version\": \"0.1.0\",\n  \"file_path\": "<<q(r.file_path)<<",\n  \"file_size\": "<<r.file_size<<",\n  \"sha256\": "<<(r.sha256.empty()?"null":q(r.sha256))<<",\n";
    o<<"  \"gguf\": {\"version\": "<<r.version<<", \"declared_tensor_count\": "<<r.tensor_count_declared<<", \"parsed_tensor_count\": "<<r.tensors.size()<<", \"declared_metadata_count\": "<<r.metadata_count_declared<<", \"parsed_metadata_count\": "<<r.metadata.size()<<", \"alignment\": "<<r.alignment<<", \"tensor_data_start\": "<<r.tensor_data_start<<"},\n";
    o<<"  \"structure_status\": "<<q(r.structure_status)<<", \"tensor_value_decode_status\": "<<q(r.tensor_value_decode_status)<<",\n";
    o<<"  \"physical_scan\": {\"bytes_scanned\": "<<r.bytes_scanned<<", \"coverage_percent\": "<<std::fixed<<std::setprecision(6)<<r.coverage_percent<<", \"chunks\": [";
    for(size_t i=0;i<r.chunks.size();++i){auto& c=r.chunks[i]; if(i)o<<",";o<<"{\"offset\":"<<c.offset<<",\"length\":"<<c.length<<",\"success\":"<<(c.success?"true":"false")<<",\"error\":"<<q(c.error)<<"}";}
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
