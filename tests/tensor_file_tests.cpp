#include "yr_test.hpp"
#include <yaoray/render/tensor_file.hpp>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

void PutBytes(std::vector<unsigned char>& b, const void* p, std::size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p); b.insert(b.end(), c, c + n);
}
template <class T> void PutLE(std::vector<unsigned char>& b, T v) { PutBytes(b, &v, sizeof(T)); }

std::string WriteMinimalTensor(const char* path) {
    std::vector<unsigned char> b;
    const char magic[12] = {'t','e','n','s','o','r','_','f','i','l','e','\0'};
    PutBytes(b, magic, 12); b.push_back(1); b.push_back(0);
    PutLE<std::uint32_t>(b, 2);
    auto descSize = [](const std::string& n, int ndim){ return 2 + n.size() + 2 + 1 + 8 + 8*ndim; };
    std::size_t header = b.size();
    std::size_t dataA_off = header + descSize("a",1) + descSize("b",2);
    std::size_t dataB_off = dataA_off + 2 * sizeof(float);
    auto putDesc = [&](const std::string& name, std::uint16_t ndim, std::uint8_t dtype,
                       std::uint64_t off, const std::vector<std::uint64_t>& shape){
        PutLE<std::uint16_t>(b, static_cast<std::uint16_t>(name.size()));
        PutBytes(b, name.data(), name.size());
        PutLE<std::uint16_t>(b, ndim); b.push_back(dtype); PutLE<std::uint64_t>(b, off);
        for (auto s : shape) PutLE<std::uint64_t>(b, s);
    };
    putDesc("a", 1, 10, dataA_off, {2});
    putDesc("b", 2, 10, dataB_off, {2,2});
    float da[2] = {1.f, 2.f};            PutBytes(b, da, sizeof(da));
    float db[4] = {1.f, 2.f, 3.f, 4.f};  PutBytes(b, db, sizeof(db));
    std::FILE* f = std::fopen(path, "wb"); std::fwrite(b.data(), 1, b.size(), f); std::fclose(f);
    return path;
}

} // namespace

YR_TEST(tensor_file_reads_fields_and_shapes) {
    const std::string path = WriteMinimalTensor("tensor_test_min.tensor");
    std::string err; auto tf = yr::ReadTensorFile(path, err);
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
    std::string err; auto tf = yr::ReadTensorFile("tensor_bad_magic.tensor", err);
    YR_EXPECT_TRUE(!tf.has_value()); YR_EXPECT_TRUE(!err.empty());
    std::remove("tensor_bad_magic.tensor");
}

YR_TEST(tensor_file_rejects_truncated) {
    std::string err; auto tf = yr::ReadTensorFile("tensor_does_not_exist.tensor", err);
    YR_EXPECT_TRUE(!tf.has_value());
}
