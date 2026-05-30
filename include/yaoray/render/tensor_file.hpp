#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yr {

enum class TensorDType : std::uint8_t {
    Invalid=0, UInt8=1, Int8=2, UInt16=3, Int16=4, UInt32=5, Int32=6,
    UInt64=7, Int64=8, Float16=9, Float32=10, Float64=11
};

std::size_t TensorDTypeSize(TensorDType d);   // 0 for Invalid

struct TensorField {
    TensorDType dtype = TensorDType::Invalid;
    std::vector<std::uint64_t> shape;
    std::vector<std::byte> data;              // raw little-endian bytes
    std::size_t ElementCount() const;         // product(shape); 1 if shape empty
    std::vector<float> AsFloat32() const;      // non-empty only when dtype==Float32
};

struct TensorFile {
    std::unordered_map<std::string, TensorField> fields;
    const TensorField* Find(const std::string& name) const;  // nullptr if absent
};

// nullopt + sets `error` on any malformed input (missing file, <18 bytes, bad magic,
// bad version, dtype out of range, offset+size past EOF). Never throws.
std::optional<TensorFile> ReadTensorFile(const std::string& path, std::string& error);

}  // namespace yr
