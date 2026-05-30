#include "yr_test.hpp"
#include "tensor_test_util.hpp"
#include <yaoray/render/measured_brdf.hpp>
#include <cstdio>

YR_TEST(measured_brdf_loads_isotropic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_iso.bsdf", 2);
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
    const std::string p = yrtest::WriteSyntheticBsdf("measured_aniso.bsdf", 4);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(!m.has_value()); YR_EXPECT_TRUE(!err.empty());
    std::remove(p.c_str());
}
YR_TEST(measured_brdf_rejects_missing_field) {
    using namespace yrtest;
    std::vector<TField> f; f.push_back({"theta_i", 1, 10, {2}, F32({0.f,0.f})});
    const std::string p = WriteTensor("measured_partial.bsdf", f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(!m.has_value());
    std::remove(p.c_str());
}
