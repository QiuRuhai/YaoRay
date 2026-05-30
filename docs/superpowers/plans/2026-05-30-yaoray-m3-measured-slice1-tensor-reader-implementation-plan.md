# YaoRay M3 Measured Slice 1 — Tensor reader + MeasuredBrdf table + compile wiring

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax. This slice is plumbing (binary parsing + IR wiring) — no BSDF math yet.

**Goal:** Load a PBRT v4 `.bsdf` tensor file (Dupuy & Jakob 2018) into an in-memory `MeasuredBrdf` (validated raw field arrays), carry it through compilation into `RenderSceneIR`, and tag the material `RenderMaterialKind::Measured`. **No rendering change yet** — the BSDF dispatch for `Measured` temporarily aliases the conductor; evaluation (Slice 2) and sampling (Slice 3) come next. A corrupt / unsupported / anisotropic file degrades gracefully to the existing conductor fallback + Warning.

**Architecture:** A generic **TensorFile** reader (parses the binary container into named typed arrays) + a **MeasuredBrdf** loader (validates the `.bsdf` field set, holds the raw arrays + shapes + `isotropic` flag). `RenderSceneIR` gains a flat `measured_brdfs[]` table; `RenderMaterial` gains `measured_index`. Slice 1 holds the RAW arrays only — the `PiecewiseLinear2D` warp (with its marginal/conditional CDFs) is built in Slices 2–3 when `f`/`Sample` need it.

**Tech Stack:** C++20, CMake 3.24, `yr_test.hpp`, CTest. No external dependency (own minimal binary reader).

---

## Scope

**Slice 1 delivers:**
- `TensorFile` reader: parse the binary container; expose fields by name with dtype + shape + raw data.
- `MeasuredBrdf` struct + `LoadMeasuredBrdf(path)`: validate the `.bsdf` field set/shapes/cross-constraints, detect isotropic, hold the raw `theta_i`/`phi_i`/`wavelengths`/`ndf`/`sigma`/`vndf`/`luminance`/`spectra` arrays + their shapes.
- IR wiring: `RenderSceneIR.measured_brdfs`, `RenderMaterial.measured_index` (-1 default), `RenderMaterialKind::Measured`.
- Compiler: `measured` branch resolves `filename`, loads, stores, sets `kind = Measured`; any failure → conductor degrade + Warning (the "never `Error:`" policy holds).
- BSDF dispatch: `Measured` temporarily routes to the existing Conductor branch in `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf` (documented Slice-1 alias, like 2a's coated alias). `IsDeltaBsdf(Measured)` → false (glossy).
- Tests: synthetic-`.bsdf` parse (fields correct), isotropic detection, and graceful degrade (bad magic / truncated / missing field / anisotropic-while-unsupported / dtype out of range).

**Slice 1 does NOT do (later):** the `PiecewiseLinear2D` warp + CDFs; `f` evaluation (Slice 2); `Sample`/`Pdf` (Slice 3); spectral→RGB conversion (Slice 2); sportscar (Slice 4); anisotropic support.

---

## The `.bsdf` tensor format (authoritative reference — implement to this)

Confirmed from `mmp/pbrt-v4` `src/pbrt/bxdfs.cpp` (`Tensor` class + `MeasuredBxDFData::Create`).

**Container (little-endian throughout):**

```
[12] magic        : bytes "tensor_file\0"  (the 11 ASCII chars of "tensor_file" + one 0x00)
[ 2] version      : uint8 major, uint8 minor  == {1, 0}
[ 4] n_fields     : uint32
repeat n_fields times (field descriptor, packed):
  [ 2] name_len   : uint16
  [name_len] name : raw ASCII (NOT null-terminated)
  [ 2] ndim       : uint16
  [ 1] dtype      : uint8   (enum below)
  [ 8] offset     : uint64  (ABSOLUTE byte offset into the file where this field's data starts)
  [ndim*8] shape  : uint64[ndim]  (row-major; last dim contiguous)
field data blocks live at their `offset` (NOT necessarily contiguous — seek to offset; do not assume packing/alignment).
```

**dtype enum (uint8):** `Invalid=0, UInt8=1, Int8=2, UInt16=3, Int16=4, UInt32=5, Int32=6, UInt64=7, Int64=8, Float16=9, Float32=10, Float64=11`. Sizes: 0,1,1,2,2,4,4,8,8,2,4,8. Reject `dtype==0 || dtype>11`.

**`.bsdf` fields** (all float arrays are **Float32**=10; flags **UInt8**=1):

| name | dtype | ndim | shape | constraint |
|---|---|---|---|---|
| `description` | UInt8 | 1 | `[N]`, N>0 | metadata string |
| `theta_i` | Float32 | 1 | `[n_theta_i]` | |
| `phi_i` | Float32 | 1 | `[n_phi_i]` | **isotropic iff `n_phi_i ≤ 2`** |
| `wavelengths` | Float32 | 1 | `[n_wl]` | n_wl=3 for RGB files, ~spectral otherwise |
| `ndf` | Float32 | 2 | `[n_theta_m, n_phi_m]` | |
| `sigma` | Float32 | 2 | `[n_theta_m, n_phi_m]` | |
| `vndf` | Float32 | 4 | `[n_phi_i, n_theta_i, n_theta_m, n_phi_m]` | `shape[0]==n_phi_i`, `shape[1]==n_theta_i` |
| `luminance` | Float32 | 4 | `[n_phi_i, n_theta_i, res, res]` | `shape[0]==n_phi_i`, `shape[1]==n_theta_i`, `shape[2]==shape[3]` |
| `spectra` | Float32 | 5 | `[n_phi_i, n_theta_i, n_wl, res, res]` | `shape[0]==n_phi_i`, `shape[1]==n_theta_i`, `shape[2]==n_wl`, `shape[3]==shape[4]`, `shape[3]==luminance.shape[2]` |
| `jacobian` | UInt8 | 1 | `[1]` | bool |

Field order in the file is not guaranteed — look up by name. Missing/wrong-shape/wrong-dtype required field → load failure → graceful degrade.

---

## File Structure

| Path | Responsibility |
|---|---|
| `include/yaoray/render/tensor_file.hpp` + `src/render/tensor_file.cpp` (new) | Generic binary tensor-file reader: `TensorFile` + `TensorField` (name, dtype, shape, raw bytes / typed accessors). Format-agnostic. |
| `include/yaoray/render/measured_brdf.hpp` + `src/render/measured_brdf.cpp` (new) | `MeasuredBrdf` struct (raw `.bsdf` fields + shapes + `isotropic`) + `std::optional<MeasuredBrdf> LoadMeasuredBrdf(path, &error)`. Validates the `.bsdf` field set. |
| `include/yaoray/render/render_scene.hpp` (modify) | `RenderSceneIR.measured_brdfs` (`std::vector<MeasuredBrdf>`); `RenderMaterial.measured_index` (int, -1); `RenderMaterialKind::Measured`. |
| `src/render/scene_compiler.cpp` (modify) | `measured` branch: resolve `filename` (source roots), `LoadMeasuredBrdf`, push, set `kind=Measured` + `measured_index`; degrade on failure. |
| `src/render/bsdf.cpp` (modify) | `Measured` cases in the three entry points + `IsDeltaBsdf` (alias Conductor for now; not delta). |
| `tests/tensor_file_tests.cpp` (new) | Reader tests against a synthetic in-memory/temp `.bsdf`. |
| `tests/measured_brdf_tests.cpp` (new) | `LoadMeasuredBrdf` validation + isotropic + degrade tests. |
| `tests/scene_compiler_measured_tests.cpp` (new) | `measured` compile → `kind=Measured` on good file; degrade on bad. |
| `CMakeLists.txt` (modify) | Add the 2 new sources + 3 new test files. |

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `e901b09`). Use `EnterWorktree` with name `m3-measured-slice1-tensor`. (Pending PRs #15/#16 are not required by this slice; the spec content is in this plan.)

Baseline (MSVC → `build/Release/`):
```bash
cmake -S . -B build && cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c   # expect 245 PASS / 0 FAIL
cd build && ctest -C Release 2>&1 | tail -3   # 9/9
cd ..
```
`yaoray_tests.exe` ignores `--filter`; run from the worktree ROOT and grep. clangd stale-index hints are false positives.

---

## Task 1: Generic `TensorFile` binary reader

**Files:** create `include/yaoray/render/tensor_file.hpp`, `src/render/tensor_file.cpp`, `tests/tensor_file_tests.cpp`; modify `CMakeLists.txt`.

Interface:
```cpp
namespace yr {
enum class TensorDType : std::uint8_t {
    Invalid=0, UInt8=1, Int8=2, UInt16=3, Int16=4, UInt32=5, Int32=6,
    UInt64=7, Int64=8, Float16=9, Float32=10, Float64=11
};
struct TensorField {
    TensorDType dtype = TensorDType::Invalid;
    std::vector<std::uint64_t> shape;
    std::vector<std::byte> data;          // raw little-endian bytes, size = dtype_size*product(shape)
    std::size_t ElementCount() const;     // product(shape)
    std::vector<float> AsFloat32() const;  // valid only when dtype==Float32; else empty
};
struct TensorFile {
    std::unordered_map<std::string, TensorField> fields;
    const TensorField* Find(const std::string& name) const;
};
// Returns nullopt + sets error on any malformed input (bad magic/version, dtype out of range,
// truncated data, offset past EOF). Never throws.
std::optional<TensorFile> ReadTensorFile(const std::string& path, std::string& error);
}
```

- [ ] **Step 1: Failing tests** — `tests/tensor_file_tests.cpp`. Add a helper that writes a minimal valid tensor file to a temp path, then asserts `ReadTensorFile` parses it. Use a self-contained byte writer:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/tensor_file.hpp>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
// Append helpers (little-endian).
void PutBytes(std::vector<unsigned char>& b, const void* p, std::size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p); b.insert(b.end(), c, c + n);
}
template <class T> void PutLE(std::vector<unsigned char>& b, T v) { PutBytes(b, &v, sizeof(T)); }

// Build a tensor file with two Float32 fields: "a" shape [2] = {1,2}, "b" shape [2,2] = {1,2,3,4}.
std::string WriteMinimalTensor(const char* path) {
    std::vector<unsigned char> b;
    const char magic[12] = {'t','e','n','s','o','r','_','f','i','l','e','\0'};
    PutBytes(b, magic, 12);
    b.push_back(1); b.push_back(0);                 // version {1,0}
    PutLE<std::uint32_t>(b, 2);                      // n_fields

    // We must know data offsets up front. Compute header size after writing descriptors with
    // placeholder offsets is fiddly; instead lay data AFTER all descriptors and back-patch,
    // OR (simpler) write descriptors with correct offsets by precomputing. Here: write both
    // field descriptors, then the two data blocks, computing offsets as we go.
    // Descriptor sizes: name_len(2)+name + ndim(2)+dtype(1)+offset(8)+shape(8*ndim).
    auto descSize = [](const std::string& n, int ndim){ return 2 + n.size() + 2 + 1 + 8 + 8*ndim; };
    std::size_t header = b.size();
    std::size_t descA = descSize("a", 1), descB = descSize("b", 2);
    std::size_t dataA_off = header + descA + descB;
    std::size_t dataB_off = dataA_off + 2 * sizeof(float);

    auto putDesc = [&](const std::string& name, std::uint16_t ndim, std::uint8_t dtype,
                       std::uint64_t off, const std::vector<std::uint64_t>& shape){
        PutLE<std::uint16_t>(b, static_cast<std::uint16_t>(name.size()));
        PutBytes(b, name.data(), name.size());
        PutLE<std::uint16_t>(b, ndim);
        b.push_back(dtype);
        PutLE<std::uint64_t>(b, off);
        for (auto s : shape) PutLE<std::uint64_t>(b, s);
    };
    putDesc("a", 1, 10, dataA_off, {2});
    putDesc("b", 2, 10, dataB_off, {2,2});
    float da[2] = {1.f, 2.f};       PutBytes(b, da, sizeof(da));
    float db[4] = {1.f, 2.f, 3.f, 4.f}; PutBytes(b, db, sizeof(db));

    std::FILE* f = std::fopen(path, "wb"); std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
    return path;
}
} // namespace

YR_TEST(tensor_file_reads_fields_and_shapes) {
    const std::string path = WriteMinimalTensor("tensor_test_min.tensor");
    std::string err;
    auto tf = yr::ReadTensorFile(path, err);
    YR_EXPECT_TRUE(tf.has_value());
    const yr::TensorField* a = tf->Find("a");
    const yr::TensorField* bb = tf->Find("b");
    YR_EXPECT_TRUE(a != nullptr && bb != nullptr);
    YR_EXPECT_EQ(static_cast<int>(a->dtype), 10);
    YR_EXPECT_EQ(a->shape.size(), static_cast<std::size_t>(1));
    YR_EXPECT_EQ(a->shape[0], static_cast<std::uint64_t>(2));
    const std::vector<float> av = a->AsFloat32();
    YR_EXPECT_EQ(av.size(), static_cast<std::size_t>(2));
    YR_EXPECT_NEAR(av[1], 2.0f, 1e-6f);
    YR_EXPECT_EQ(bb->shape.size(), static_cast<std::size_t>(2));
    YR_EXPECT_EQ(bb->ElementCount(), static_cast<std::size_t>(4));
    std::remove(path.c_str());
}

YR_TEST(tensor_file_rejects_bad_magic) {
    std::FILE* f = std::fopen("tensor_bad_magic.tensor", "wb");
    const char junk[18] = "not_a_tensorfile!"; std::fwrite(junk, 1, 18, f); std::fclose(f);
    std::string err;
    auto tf = yr::ReadTensorFile("tensor_bad_magic.tensor", err);
    YR_EXPECT_TRUE(!tf.has_value());
    YR_EXPECT_TRUE(!err.empty());
    std::remove("tensor_bad_magic.tensor");
}

YR_TEST(tensor_file_rejects_truncated) {
    std::string err;
    auto tf = yr::ReadTensorFile("tensor_does_not_exist.tensor", err);
    YR_EXPECT_TRUE(!tf.has_value());
}
```

Register `tests/tensor_file_tests.cpp` in `CMakeLists.txt`. (Verify `YR_EXPECT_NEAR`/`YR_EXPECT_EQ` spellings against an existing test; adapt. Confirm temp-file writes are allowed in the test working dir — other tests already write images, so file IO is fine.)

- [ ] **Step 2: Run, verify FAIL** (reader not implemented).

- [ ] **Step 3: Implement `ReadTensorFile`** in `src/render/tensor_file.cpp` to the format spec above. Read the whole file into a buffer (or `fseek`/`fread` per field via the absolute `offset`). Validate: ≥18 bytes; magic exactly the 12 bytes; version `{1,0}`; each `dtype` in `[1,11]`; `offset + dtype_size*product(shape) <= file_size`. On any violation, set `error` and return `nullopt`. `AsFloat32()` returns the reinterpreted floats only when `dtype==Float32`. Little-endian: the dev target is x86 (LE) — a direct `memcpy` is correct; add a brief comment that big-endian hosts are out of scope.

- [ ] **Step 4: Run, verify PASS** + full suite (245 + 3) + CTest 9/9.

- [ ] **Step 5: Commit** — `feat(render): generic tensor-file binary reader (M3 measured)`.

---

## Task 2: `MeasuredBrdf` struct + `LoadMeasuredBrdf` (validation + isotropic)

**Files:** create `include/yaoray/render/measured_brdf.hpp`, `src/render/measured_brdf.cpp`, `tests/measured_brdf_tests.cpp`; modify `CMakeLists.txt`.

Slice 1 holds the **raw** arrays + shapes (no `PiecewiseLinear2D` yet):
```cpp
namespace yr {
struct MeasuredBrdf {
    std::vector<float> theta_i, phi_i, wavelengths;
    std::vector<float> ndf, sigma, vndf, luminance, spectra;
    std::vector<std::uint64_t> ndf_shape, sigma_shape, vndf_shape, luminance_shape, spectra_shape;
    bool isotropic = false;
    bool jacobian  = false;
    int  res = 0;        // luminance_shape[2] (== spectra_shape[3]); square spatial grid
};
// nullopt + error on: read failure, missing/wrong-dtype/wrong-ndim required field, failed
// cross-constraint, OR anisotropic (n_phi_i > 2 — isotropic-only in this milestone phase).
std::optional<MeasuredBrdf> LoadMeasuredBrdf(const std::string& path, std::string& error);
}
```

`LoadMeasuredBrdf` = `ReadTensorFile` then validate the `.bsdf` field set per the format table:
- Required Float32 fields present with correct `ndim`: `theta_i`(1), `phi_i`(1), `wavelengths`(1), `ndf`(2), `sigma`(2), `vndf`(4), `luminance`(4), `spectra`(5). `jacobian` UInt8 `[1]` (optional → default false).
- Cross-constraints: `vndf.shape[0]==phi_i.count`, `vndf.shape[1]==theta_i.count`; `luminance.shape[{0,1}]==={phi_i,theta_i}.count`, `luminance.shape[2]==luminance.shape[3]`; `spectra.shape[0..2]==={phi_i,theta_i,wavelengths}.count`, `spectra.shape[3]==spectra.shape[4]==luminance.shape[2]`.
- `isotropic = phi_i.count <= 2`. **If not isotropic → return failure** with error `"anisotropic measured BRDF not supported (isotropic-only)"`.
- `res = luminance.shape[2]`. Copy the raw arrays + shapes; read `jacobian[0] != 0`.

- [ ] **Step 1: Failing tests** — `tests/measured_brdf_tests.cpp`. Provide a generic tensor writer + a synthetic `.bsdf` builder. Add `tests/tensor_test_util.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace yrtest {
struct TField { std::string name; std::uint16_t ndim; std::uint8_t dtype;
                std::vector<std::uint64_t> shape; std::vector<unsigned char> data; };

inline std::size_t DTypeSize(std::uint8_t d) {
    switch (d) { case 1: case 2: return 1; case 3: case 4: case 9: return 2;
                 case 5: case 6: case 10: return 4; default: return 8; }
}
// Writes a valid little-endian tensor file. Data blocks laid contiguously after all descriptors.
inline std::string WriteTensor(const char* path, const std::vector<TField>& fields) {
    auto put = [](std::vector<unsigned char>& b, const void* p, std::size_t n){
        const unsigned char* c = static_cast<const unsigned char*>(p); b.insert(b.end(), c, c+n); };
    std::vector<unsigned char> b;
    const char magic[12] = {'t','e','n','s','o','r','_','f','i','l','e','\0'};
    put(b, magic, 12); b.push_back(1); b.push_back(0);
    std::uint32_t nf = static_cast<std::uint32_t>(fields.size()); put(b, &nf, 4);
    auto descSize = [](const TField& f){ return 2 + f.name.size() + 2 + 1 + 8 + 8*f.ndim; };
    std::size_t off = b.size(); for (auto& f : fields) off += descSize(f);  // first data offset
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
inline std::vector<unsigned char> U8(const std::vector<unsigned char>& v) { return v; }

// Minimal valid .bsdf. n_phi_i=2 => isotropic; pass 4 for anisotropic. Tiny dims.
inline std::string WriteSyntheticBsdf(const char* path, int n_phi_i) {
    const int nti = 2, nwl = 3, ntm = 2, npm = 2, res = 2;
    auto zeros = [](std::size_t n){ return std::vector<float>(n, 0.0f); };
    std::vector<TField> f;
    f.push_back({"description", 1, 1, {3}, U8({'a','b','c'})});
    f.push_back({"theta_i", 1, 10, {(std::uint64_t)nti}, F32(zeros(nti))});
    f.push_back({"phi_i",   1, 10, {(std::uint64_t)n_phi_i}, F32(zeros(n_phi_i))});
    f.push_back({"wavelengths", 1, 10, {(std::uint64_t)nwl}, F32({600.f,550.f,450.f})});
    f.push_back({"ndf",   2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    f.push_back({"sigma", 2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    f.push_back({"vndf",  4, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)ntm,(std::uint64_t)npm},
                 F32(zeros(n_phi_i*nti*ntm*npm))});
    f.push_back({"luminance", 4, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)res,(std::uint64_t)res},
                 F32(zeros(n_phi_i*nti*res*res))});
    f.push_back({"spectra", 5, 10, {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)nwl,(std::uint64_t)res,(std::uint64_t)res},
                 F32(zeros(n_phi_i*nti*nwl*res*res))});
    f.push_back({"jacobian", 1, 1, {1}, U8({1})});
    return WriteTensor(path, f);
}
} // namespace yrtest
```

Tests:
```cpp
#include "yr_test.hpp"
#include "tensor_test_util.hpp"
#include <yaoray/render/measured_brdf.hpp>
#include <cstdio>

YR_TEST(measured_brdf_loads_isotropic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_iso.bsdf", /*n_phi_i=*/2);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(m.has_value());
    YR_EXPECT_TRUE(m->isotropic);
    YR_EXPECT_EQ(m->theta_i.size(), static_cast<std::size_t>(2));
    YR_EXPECT_EQ(m->wavelengths.size(), static_cast<std::size_t>(3));
    YR_EXPECT_EQ(m->res, 2);
    YR_EXPECT_TRUE(m->jacobian);
    std::remove(p.c_str());
}
YR_TEST(measured_brdf_rejects_anisotropic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_aniso.bsdf", /*n_phi_i=*/4);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(!m.has_value());
    YR_EXPECT_TRUE(!err.empty());
    std::remove(p.c_str());
}
YR_TEST(measured_brdf_rejects_missing_field) {
    // Build a .bsdf then drop a required field by writing a partial set.
    using namespace yrtest;
    std::vector<TField> f;
    f.push_back({"theta_i", 1, 10, {2}, F32({0.f,0.f})});  // intentionally incomplete
    const std::string p = WriteTensor("measured_partial.bsdf", f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(!m.has_value());
    std::remove(p.c_str());
}
```
Register `tests/measured_brdf_tests.cpp` in `CMakeLists.txt`. (`tensor_test_util.hpp` is header-only; ensure the test include dir covers `tests/`. Verify `<cstring>` is included where `std::memcpy` is used.)

- [ ] **Step 2: Run, verify FAIL.**
- [ ] **Step 3: Implement** `LoadMeasuredBrdf` per the validation list above.
- [ ] **Step 4: Run, verify PASS** + full suite (245 + 3 + 3) + CTest 9/9.
- [ ] **Step 5: Commit** — `feat(render): MeasuredBrdf loader + .bsdf validation (isotropic-only)`.

---

## Task 3: IR wiring + compiler + BSDF dispatch alias

**Files:** modify `include/yaoray/render/render_scene.hpp`, `src/render/scene_compiler.cpp`, `src/render/bsdf.cpp`, `CMakeLists.txt`; create `tests/scene_compiler_measured_tests.cpp`.

- [ ] **Step 1: IR + enum.** In `render_scene.hpp`: add `Measured` to `RenderMaterialKind`; add `int measured_index = -1;` to `RenderMaterial`; add `std::vector<MeasuredBrdf> measured_brdfs;` to `RenderSceneIR` (include `measured_brdf.hpp`). Add `Measured` cases (returning the Conductor behavior) to every `switch (material.kind)` the compiler's `-Wswitch` flags.

- [ ] **Step 2: Failing compiler test** — `tests/scene_compiler_measured_tests.cpp`: build a `PbrtScene` with a `measured` material whose `"string filename"` points at a synthetic isotropic `.bsdf` written via `yrtest::WriteSyntheticBsdf` (set `pbrt.source_root` so the path resolves). Compile; assert the material `kind == RenderMaterialKind::Measured`, `measured_index >= 0`, and `result.scene->measured_brdfs` non-empty. A second test: a `measured` material pointing at a missing/anisotropic file → `kind == Conductor` (degraded) + a `MaterialFallbackWarning`-style Warning present, no `Error`. (Mirror the fixture style of `tests/scene_compiler_coated_tests.cpp`; verify how `filename` + source-root resolution works in the compiler — reuse the existing path-resolution helper the `imagemap`/`plymesh` loaders use.)

- [ ] **Step 3: Compiler branch.** Replace the `measured` degrade branch (`scene_compiler.cpp` ~line 779) with: read `"string filename"`; resolve against the scene source roots (same helper as texture/ply file resolution); `LoadMeasuredBrdf`; on success push to `ir.measured_brdfs`, set `material.kind = Measured`, `material.measured_index = index`; on failure emit a `Warning` (reuse `MaterialFallbackWarning` or an analogous "measured file X failed: <err>; using conductor fallback") and keep the existing conductor fallback values. Never `Error`.

- [ ] **Step 4: BSDF dispatch alias.** In `bsdf.cpp`, add `RenderMaterialKind::Measured` to the Conductor group in `EvaluateBsdf`, `PdfBsdf`, `SampleBsdf` (it behaves as a conductor until Slice 2/3), with a `// TODO(measured Slice 2/3): real MeasuredBxDF f/Sample/Pdf` comment. `IsDeltaBsdf`: `Measured` → `false` (it is glossy, not a delta — light sampling applies). Confirm the conductor branch tolerates the default fallback eta/k the compiler still sets.

- [ ] **Step 5: Run** — new compiler tests PASS; full suite (245 + 6 + N) ; CTest 9/9. No render regression (no scene uses `measured` in CTest).

- [ ] **Step 6: Commit** — `feat(render): wire Measured material kind + compile measured to MeasuredBrdf (alias conductor)`.

---

## Task 4: PR + merge

- [ ] **Step 1:** `git log --oneline e901b09..HEAD` — expect 3 commits (reader, loader, wiring).
- [ ] **Step 2:** push `worktree-m3-measured-slice1-tensor`; `gh pr create --base main` with a body covering: the tensor reader + MeasuredBrdf loader (isotropic-only, graceful degrade) + IR/dispatch wiring (conductor alias until Slice 2/3); the format reference; tests (synthetic `.bsdf`); scope (no eval/sample yet). Note PRs #15/#16 may merge first — rebase if needed.
- [ ] **Step 3:** address review; re-run tests.
- [ ] **Step 4:** merge (operator-gated); finish per `superpowers:finishing-a-development-branch` (ff main, remove worktree — manual fallback if `ExitWorktree` no-ops, delete remote branch).

---

## Self-Review Notes

- **Spec coverage:** Slice 1 of the measured spec — TensorReader (Task 1), MeasuredBrdf table + load + isotropic detection + graceful degrade (Task 2), IR/`measured_index`/`Measured` kind + compile wiring + dispatch alias (Task 3), PR (Task 4). Eval/sample/sportscar are Slices 2–4.
- **Refinement vs spec:** the real `.bsdf` files are spectral and ~6.6 MB, so Slice 1 tests use a **synthetic** `.bsdf` (format fully pinned from pbrt-v4) rather than vendoring a large real file; vendoring/real-data validation moves to Slice 2 (eval), where the spectral→RGB decision is also made. The spec's "(a) vendor a small real `.bsdf`" is therefore deferred to Slice 2; (b) synthetic is used here.
- **Deferred deliberately:** `PiecewiseLinear2D` warp + CDFs (Slices 2–3 build them from the raw arrays Slice 1 stores); spectral→RGB; anisotropic (load fails → degrade).
- **Type consistency:** `TensorFile`/`TensorField`/`ReadTensorFile`; `MeasuredBrdf`/`LoadMeasuredBrdf`; `RenderSceneIR.measured_brdfs`, `RenderMaterial.measured_index`, `RenderMaterialKind::Measured` — used identically across tasks.
- **No-Error policy:** every failure path (bad file, anisotropic, missing field) degrades to conductor + Warning; verified by the Task 3 degrade test.
- **Endianness:** dev target is x86 LE; direct memcpy is correct; big-endian is out of scope (commented).
- **Worktree branch** `m3-measured-slice1-tensor` consistent across Setup and Task 4.
