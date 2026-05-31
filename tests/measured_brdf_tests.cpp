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
YR_TEST(measured_brdf_rejects_shape_mismatch) {
    // Full field set but with vndf.shape[0] != n_phi_i (should be 2, written as 3).
    // This violates the cross-constraint checked by LoadMeasuredBrdf.
    using namespace yrtest;
    const int nti = 2, nwl = 3, ntm = 2, npm = 2, res = 2;
    const int n_phi_i = 2; // isotropic
    const int bad_phi  = 3; // wrong: vndf expects n_phi_i in dim 0
    auto zeros = [](std::size_t n){ return std::vector<float>(n, 0.0f); };
    std::vector<TField> fields;
    fields.push_back({"description", 1, 1, {3}, {'a','b','c'}});
    fields.push_back({"theta_i",   1, 10, {(std::uint64_t)nti}, F32(zeros(nti))});
    fields.push_back({"phi_i",     1, 10, {(std::uint64_t)n_phi_i}, F32(zeros(n_phi_i))});
    fields.push_back({"wavelengths", 1, 10, {(std::uint64_t)nwl}, F32({600.f,550.f,450.f})});
    fields.push_back({"ndf",   2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    fields.push_back({"sigma", 2, 10, {(std::uint64_t)ntm,(std::uint64_t)npm}, F32(zeros(ntm*npm))});
    // vndf.shape[0] = bad_phi (3) instead of n_phi_i (2) — cross-constraint violation
    fields.push_back({"vndf",  4, 10,
        {(std::uint64_t)bad_phi,(std::uint64_t)nti,(std::uint64_t)ntm,(std::uint64_t)npm},
        F32(zeros((std::size_t)bad_phi*nti*ntm*npm))});
    fields.push_back({"luminance", 4, 10,
        {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)res,(std::uint64_t)res},
        F32(zeros((std::size_t)n_phi_i*nti*res*res))});
    fields.push_back({"spectra", 5, 10,
        {(std::uint64_t)n_phi_i,(std::uint64_t)nti,(std::uint64_t)nwl,(std::uint64_t)res,(std::uint64_t)res},
        F32(zeros((std::size_t)n_phi_i*nti*nwl*res*res))});
    fields.push_back({"jacobian", 1, 1, {1}, {1}});
    const std::string p = WriteTensor("measured_mismatch.bsdf", fields);
    std::string err;
    auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(!m.has_value());
    YR_EXPECT_TRUE(!err.empty());
    std::remove(p.c_str());
}
YR_TEST(measured_brdf_loads_independent_vndf_res) {
    // Regression: vndf spatial resolution must be independent of ndf dims.
    // Real .bsdf files use e.g. ndf=[2,512] but vndf=[1,8,512,512] — the old
    // validator required vndf[2]==n_theta_m and vndf[3]==n_phi_m (ndf shape),
    // which rejected every real file.  WriteSyntheticBsdf now generates such
    // a layout (ntm=2, npm=3, vndf_res=4 != ntm/npm).
    const std::string p = yrtest::WriteSyntheticBsdf("measured_indep_vndf.bsdf", 2);
    std::string err;
    auto m = yr::LoadMeasuredBrdf(p, err);
    // Must LOAD successfully — not degrade to conductor.
    YR_EXPECT_TRUE(m.has_value());
    if (m.has_value()) {
        // vndf spatial dims (4x4) must differ from ndf dims (2x3)
        YR_EXPECT_TRUE(m->vndf_shape[2] != m->ndf_shape[0] ||
                       m->vndf_shape[3] != m->ndf_shape[1]);
        YR_EXPECT_EQ(m->res, 2); // luminance/spectra res stays 2
    }
    std::remove(p.c_str());
}
