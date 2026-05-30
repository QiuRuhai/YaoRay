#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace yrtest {
struct TField { std::string name; std::uint16_t ndim; std::uint8_t dtype;
                std::vector<std::uint64_t> shape; std::vector<unsigned char> data; };

inline std::size_t DTypeSize(std::uint8_t d) {
    switch (d) { case 1: case 2: return 1; case 3: case 4: case 9: return 2;
                 case 5: case 6: case 10: return 4; default: return 8; }
}
inline std::string WriteTensor(const char* path, const std::vector<TField>& fields) {
    auto put = [](std::vector<unsigned char>& b, const void* p, std::size_t n){
        const unsigned char* c = static_cast<const unsigned char*>(p); b.insert(b.end(), c, c+n); };
    std::vector<unsigned char> b;
    const char magic[12] = {'t','e','n','s','o','r','_','f','i','l','e','\0'};
    put(b, magic, 12); b.push_back(1); b.push_back(0);
    std::uint32_t nf = static_cast<std::uint32_t>(fields.size()); put(b, &nf, 4);
    auto descSize = [](const TField& f){ return 2 + f.name.size() + 2 + 1 + 8 + 8*f.ndim; };
    std::size_t off = b.size(); for (auto& f : fields) off += descSize(f);
    std::vector<std::uint64_t> offsets; std::size_t cur = off;
    for (auto& f : fields) { offsets.push_back(cur); cur += f.data.size(); }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const TField& f = fields[i];
        std::uint16_t nl = static_cast<std::uint16_t>(f.name.size()); put(b, &nl, 2);
        put(b, f.name.data(), f.name.size()); put(b, &f.ndim, 2); b.push_back(f.dtype);
        std::uint64_t o = offsets[i]; put(b, &o, 8);
        for (auto s : f.shape) put(b, &s, 8);
    }
    for (auto& f : fields) put(b, f.data.data(), f.data.size());
    std::FILE* fp = std::fopen(path, "wb"); std::fwrite(b.data(), 1, b.size(), fp); std::fclose(fp);
    return path;
}
inline std::vector<unsigned char> F32(const std::vector<float>& v) {
    std::vector<unsigned char> b(v.size()*4); std::memcpy(b.data(), v.data(), b.size()); return b;
}
// Minimal valid .bsdf. n_phi_i=2 => isotropic; 4 => anisotropic. Tiny dims.
inline std::string WriteSyntheticBsdf(const char* path, int n_phi_i) {
    const int nti = 2, nwl = 3, ntm = 2, npm = 2, res = 2;
    auto zeros = [](std::size_t n){ return std::vector<float>(n, 0.0f); };
    std::vector<TField> f;
    f.push_back({"description", 1, 1, {3}, {'a','b','c'}});
    f.push_back({"theta_i", 1, 10, {(std::uint64_t)nti}, F32(zeros(nti))});
    f.push_back({"phi_i",   1, 10, {(std::uint64_t)n_phi_i}, F32(zeros(n_phi_i))});
    f.push_back({"wavelengths", 1, 10, {(std::uint64_t)nwl}, F32({600.f,550.f,450.f})});
    f.push_back({"ndf",   2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    f.push_back({"sigma", 2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    f.push_back({"vndf",  4, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)ntm,(std::uint64_t)npm},
                 F32(zeros((std::size_t)n_phi_i*nti*ntm*npm))});
    f.push_back({"luminance", 4, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)res,(std::uint64_t)res},
                 F32(zeros((std::size_t)n_phi_i*nti*res*res))});
    f.push_back({"spectra", 5, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)nwl,(std::uint64_t)res,(std::uint64_t)res},
                 F32(zeros((std::size_t)n_phi_i*nti*nwl*res*res))});
    f.push_back({"jacobian", 1, 1, {1}, {1}});
    return WriteTensor(path, f);
}
} // namespace yrtest
