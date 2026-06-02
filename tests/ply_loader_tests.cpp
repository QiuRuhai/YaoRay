#include "yr_test.hpp"

#include <yaoray/assets/ply_loader.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Write a single T value in big-endian byte order to the stream.
template <typename T>
void WriteBigEndian(std::ofstream& out, T value) {
    static_assert(sizeof(T) <= 8);
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    // Reverse bytes to convert native little-endian to big-endian.
    for (std::size_t i = 0; i < sizeof(T) / 2; ++i) {
        std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
    }
    out.write(reinterpret_cast<const char*>(bytes), sizeof(T));
}

// Write a single T value in little-endian byte order (x86 native) to the stream.
template <typename T>
void WriteLittleEndian(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Build a minimal binary_big_endian PLY:
//   3 vertices: (1,2,3), (4,5,6), (7,8,9)
//   1 triangle face: indices 0,1,2
std::filesystem::path WriteBigEndianPlyToTemp() {
    auto tmp = std::filesystem::temp_directory_path() / "yaoray_test_be.ply";
    std::ofstream out{tmp, std::ios::binary | std::ios::trunc};

    // Text header (newlines are \n; StripCarriageReturn handles any \r).
    const std::string header =
        "ply\n"
        "format binary_big_endian 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n";
    out.write(header.data(), static_cast<std::streamsize>(header.size()));

    // Vertex 0: (1, 2, 3)
    WriteBigEndian(out, 1.0f);
    WriteBigEndian(out, 2.0f);
    WriteBigEndian(out, 3.0f);
    // Vertex 1: (4, 5, 6)
    WriteBigEndian(out, 4.0f);
    WriteBigEndian(out, 5.0f);
    WriteBigEndian(out, 6.0f);
    // Vertex 2: (7, 8, 9)
    WriteBigEndian(out, 7.0f);
    WriteBigEndian(out, 8.0f);
    WriteBigEndian(out, 9.0f);

    // Face: count=3 (uchar, no swap), indices 0, 1, 2 (int32, big-endian)
    unsigned char count = 3;
    out.write(reinterpret_cast<const char*>(&count), 1);
    WriteBigEndian(out, std::int32_t{0});
    WriteBigEndian(out, std::int32_t{1});
    WriteBigEndian(out, std::int32_t{2});

    return tmp;
}

// Build an equivalent binary_little_endian PLY with same data.
std::filesystem::path WriteLittleEndianPlyToTemp() {
    auto tmp = std::filesystem::temp_directory_path() / "yaoray_test_le.ply";
    std::ofstream out{tmp, std::ios::binary | std::ios::trunc};

    const std::string header =
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 3\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 1\n"
        "property list uchar int vertex_indices\n"
        "end_header\n";
    out.write(header.data(), static_cast<std::streamsize>(header.size()));

    WriteLittleEndian(out, 1.0f);
    WriteLittleEndian(out, 2.0f);
    WriteLittleEndian(out, 3.0f);
    WriteLittleEndian(out, 4.0f);
    WriteLittleEndian(out, 5.0f);
    WriteLittleEndian(out, 6.0f);
    WriteLittleEndian(out, 7.0f);
    WriteLittleEndian(out, 8.0f);
    WriteLittleEndian(out, 9.0f);

    unsigned char count = 3;
    out.write(reinterpret_cast<const char*>(&count), 1);
    WriteLittleEndian(out, std::int32_t{0});
    WriteLittleEndian(out, std::int32_t{1});
    WriteLittleEndian(out, std::int32_t{2});

    return tmp;
}

} // namespace

YR_TEST(ply_loader_binary_big_endian_loads_positions) {
    const std::filesystem::path path = WriteBigEndianPlyToTemp();
    const yr::AssetLoadResult result = yr::LoadPlyResource(path);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.resource.has_value());
    if (!result.resource.has_value()) return;

    const yr::AssetResource& res = *result.resource;
    YR_EXPECT_TRUE(!res.meshes.empty());
    YR_EXPECT_TRUE(!res.meshes[0].primitives.empty());

    const yr::AssetPrimitive& prim = res.meshes[0].primitives[0];
    YR_EXPECT_EQ(prim.positions.size(), std::size_t{3});

    YR_EXPECT_NEAR(prim.positions[0].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[0].y, 2.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[0].z, 3.0, 1e-6);

    YR_EXPECT_NEAR(prim.positions[1].x, 4.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[1].y, 5.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[1].z, 6.0, 1e-6);

    YR_EXPECT_NEAR(prim.positions[2].x, 7.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[2].y, 8.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[2].z, 9.0, 1e-6);
}

YR_TEST(ply_loader_binary_big_endian_loads_indices) {
    const std::filesystem::path path = WriteBigEndianPlyToTemp();
    const yr::AssetLoadResult result = yr::LoadPlyResource(path);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.resource.has_value());
    if (!result.resource.has_value()) return;

    const yr::AssetPrimitive& prim = result.resource->meshes[0].primitives[0];
    // 1 triangle = 3 indices
    YR_EXPECT_EQ(prim.indices.size(), std::size_t{3});
    YR_EXPECT_EQ(prim.indices[0], std::uint32_t{0});
    YR_EXPECT_EQ(prim.indices[1], std::uint32_t{1});
    YR_EXPECT_EQ(prim.indices[2], std::uint32_t{2});
}

YR_TEST(ply_loader_binary_little_endian_still_works) {
    const std::filesystem::path path = WriteLittleEndianPlyToTemp();
    const yr::AssetLoadResult result = yr::LoadPlyResource(path);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.resource.has_value());
    if (!result.resource.has_value()) return;

    const yr::AssetPrimitive& prim = result.resource->meshes[0].primitives[0];
    YR_EXPECT_EQ(prim.positions.size(), std::size_t{3});

    YR_EXPECT_NEAR(prim.positions[0].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[0].y, 2.0, 1e-6);
    YR_EXPECT_NEAR(prim.positions[0].z, 3.0, 1e-6);

    YR_EXPECT_EQ(prim.indices.size(), std::size_t{3});
    YR_EXPECT_EQ(prim.indices[0], std::uint32_t{0});
    YR_EXPECT_EQ(prim.indices[1], std::uint32_t{1});
    YR_EXPECT_EQ(prim.indices[2], std::uint32_t{2});
}
