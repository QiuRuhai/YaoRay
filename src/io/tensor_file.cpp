#include <yaoray/io/tensor_file.hpp>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace yr {

// ---------------------------------------------------------------------------
// dtype helpers
// ---------------------------------------------------------------------------

std::size_t TensorDTypeSize(TensorDType d) {
    // Sizes indexed by uint8 value of dtype enum:
    // Invalid=0, UInt8=1, Int8=2, UInt16=3, Int16=4, UInt32=5, Int32=6,
    // UInt64=7, Int64=8, Float16=9, Float32=10, Float64=11
    static constexpr std::size_t kSizes[12] = {0,1,1,2,2,4,4,8,8,2,4,8};
    const auto idx = static_cast<std::uint8_t>(d);
    if (idx > 11) return 0;
    return kSizes[idx];
}

// ---------------------------------------------------------------------------
// TensorField
// ---------------------------------------------------------------------------

std::size_t TensorField::ElementCount() const {
    if (shape.empty()) return 1;
    std::size_t n = 1;
    for (auto s : shape) n *= static_cast<std::size_t>(s);
    return n;
}

std::vector<float> TensorField::AsFloat32() const {
    // Direct byte copy is correct on little-endian x86 hosts.
    // Big-endian hosts are out of scope for this implementation.
    if (dtype != TensorDType::Float32) return {};
    const std::size_t count = ElementCount();
    std::vector<float> result(count);
    std::memcpy(result.data(), data.data(), count * sizeof(float));
    return result;
}

// ---------------------------------------------------------------------------
// TensorFile
// ---------------------------------------------------------------------------

const TensorField* TensorFile::Find(const std::string& name) const {
    auto it = fields.find(name);
    if (it == fields.end()) return nullptr;
    return &it->second;
}

// ---------------------------------------------------------------------------
// ReadTensorFile
// ---------------------------------------------------------------------------

// Helpers for reading little-endian values from a byte buffer.
namespace {

template <class T>
T ReadLE(const std::vector<std::byte>& buf, std::size_t offset) {
    T val{};
    std::memcpy(&val, buf.data() + offset, sizeof(T));
    return val;  // host is x86 LE — no byte swap needed
}

}  // namespace

std::optional<TensorFile> ReadTensorFile(const std::string& path, std::string& error) {
    // --- open file ---
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        error = "tensor_file: cannot open '" + path + "'";
        return std::nullopt;
    }

    // --- read entire file into buffer ---
    std::fseek(fp, 0, SEEK_END);
    const long file_len_signed = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (file_len_signed < 0) {
        std::fclose(fp);
        error = "tensor_file: ftell failed on '" + path + "'";
        return std::nullopt;
    }
    const std::size_t file_size = static_cast<std::size_t>(file_len_signed);

    // Minimum header: 12 magic + 2 version + 4 n_fields = 18 bytes
    if (file_size < 18) {
        std::fclose(fp);
        error = "tensor_file: file too small (" + std::to_string(file_size) + " bytes), expected >= 18";
        return std::nullopt;
    }

    std::vector<std::byte> buf(file_size);
    const std::size_t nread = std::fread(buf.data(), 1, file_size, fp);
    std::fclose(fp);

    if (nread != file_size) {
        error = "tensor_file: read error on '" + path + "' (read " +
                std::to_string(nread) + " of " + std::to_string(file_size) + " bytes)";
        return std::nullopt;
    }

    // --- validate magic: "tensor_file\0" (11 ASCII + NUL) ---
    static constexpr char kMagic[12] = {'t','e','n','s','o','r','_','f','i','l','e','\0'};
    if (std::memcmp(buf.data(), kMagic, 12) != 0) {
        error = "tensor_file: bad magic in '" + path + "'";
        return std::nullopt;
    }

    // --- validate version: {1, 0} ---
    const std::uint8_t ver_major = static_cast<std::uint8_t>(buf[12]);
    const std::uint8_t ver_minor = static_cast<std::uint8_t>(buf[13]);
    if (ver_major != 1 || ver_minor != 0) {
        error = "tensor_file: unsupported version " + std::to_string(ver_major) +
                "." + std::to_string(ver_minor) + " in '" + path + "' (expected 1.0)";
        return std::nullopt;
    }

    // --- n_fields ---
    const std::uint32_t n_fields = ReadLE<std::uint32_t>(buf, 14);

    // --- parse field descriptors ---
    TensorFile tf;
    std::size_t pos = 18;  // byte cursor into the header region

    for (std::uint32_t fi = 0; fi < n_fields; ++fi) {
        // name_len (uint16)
        if (pos + 2 > file_size) {
            error = "tensor_file: truncated descriptor at field " + std::to_string(fi);
            return std::nullopt;
        }
        const std::uint16_t name_len = ReadLE<std::uint16_t>(buf, pos);
        pos += 2;

        // name (raw ASCII, NOT null-terminated)
        if (pos + name_len > file_size) {
            error = "tensor_file: truncated name at field " + std::to_string(fi);
            return std::nullopt;
        }
        std::string name(reinterpret_cast<const char*>(buf.data() + pos), name_len);
        pos += name_len;

        // ndim (uint16)
        if (pos + 2 > file_size) {
            error = "tensor_file: truncated ndim at field '" + name + "'";
            return std::nullopt;
        }
        const std::uint16_t ndim = ReadLE<std::uint16_t>(buf, pos);
        pos += 2;

        // dtype (uint8)
        if (pos + 1 > file_size) {
            error = "tensor_file: truncated dtype at field '" + name + "'";
            return std::nullopt;
        }
        const std::uint8_t dtype_raw = static_cast<std::uint8_t>(buf[pos]);
        pos += 1;

        if (dtype_raw == 0 || dtype_raw > 11) {
            error = "tensor_file: invalid dtype " + std::to_string(dtype_raw) +
                    " at field '" + name + "'";
            return std::nullopt;
        }
        const TensorDType dtype = static_cast<TensorDType>(dtype_raw);

        // offset (uint64) — absolute byte offset of data in the file
        if (pos + 8 > file_size) {
            error = "tensor_file: truncated offset at field '" + name + "'";
            return std::nullopt;
        }
        const std::uint64_t data_offset = ReadLE<std::uint64_t>(buf, pos);
        pos += 8;

        // shape: ndim * uint64
        if (pos + static_cast<std::size_t>(ndim) * 8 > file_size) {
            error = "tensor_file: truncated shape at field '" + name + "'";
            return std::nullopt;
        }
        std::vector<std::uint64_t> shape(ndim);
        for (std::uint16_t d = 0; d < ndim; ++d) {
            shape[d] = ReadLE<std::uint64_t>(buf, pos);
            pos += 8;
        }

        // compute data byte size — overflow-checked (Fix 2: shape-product overflow)
        std::uint64_t elem_count = 1;
        for (std::uint64_t d : shape) {
            if (d != 0 && elem_count > file_size / d) {
                error = "tensor_file: field '" + name + "' shape product overflows file size";
                return std::nullopt;
            }
            elem_count *= d;
        }
        const std::size_t elem_size = TensorDTypeSize(dtype);
        // Guard elem_count * elem_size overflow before converting to size_t
        if (elem_size > 0 && elem_count > file_size / elem_size) {
            error = "tensor_file: field '" + name + "' data_bytes overflows file size";
            return std::nullopt;
        }
        const std::size_t data_bytes = static_cast<std::size_t>(elem_count) * elem_size;

        // Fix 1: non-overflowing EOF guard (data_offset + data_bytes could wrap on uint64)
        if (data_offset > file_size || data_bytes > file_size - data_offset) {
            error = "tensor_file: field '" + name + "' data (offset=" +
                    std::to_string(data_offset) + " size=" + std::to_string(data_bytes) +
                    ") extends past EOF (" + std::to_string(file_size) + ")";
            return std::nullopt;
        }

        // copy field data
        TensorField field;
        field.dtype = dtype;
        field.shape = std::move(shape);
        field.data.resize(data_bytes);
        std::memcpy(field.data.data(), buf.data() + data_offset, data_bytes);

        tf.fields[std::move(name)] = std::move(field);
    }

    return tf;
}

}  // namespace yr
