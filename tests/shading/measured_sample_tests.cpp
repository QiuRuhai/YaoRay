#include "yr_test.hpp"
#include "io/tensor_test_util.hpp"
#include <yaoray/shading/measured_brdf.hpp>
#include <yaoray/shading/bsdf.hpp>
#include <yaoray/shading/shading_material.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <cmath>
#include <cstdio>
namespace { yr::Vec3f Up(){ return yr::Vec3f{0,0,1}; } }

YR_TEST(measured_sample_valid_directions) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err); YR_EXPECT_TRUE(m.has_value());
    yr::ShadingMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up(); const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f,0,1});
    yr::Rng rng{4u}; int valid=0;
    for (int i=0;i<4000;++i){ auto s = yr::SampleBsdf(mat, wo, n, rng.NextFloat2(), rng);
        if(!s.valid) continue; ++valid;
        YR_EXPECT_TRUE(std::isfinite(s.wi.x)&&std::isfinite(s.wi.y)&&std::isfinite(s.wi.z));
        YR_EXPECT_TRUE(yr::Dot(s.wi,n) > -1e-4f);
        YR_EXPECT_TRUE(s.pdf>0.0f && std::isfinite(s.pdf));
        YR_EXPECT_TRUE(std::isfinite(s.weight.x) && s.weight.x>=0.0f); }
    YR_EXPECT_TRUE(valid>0); std::remove(p.c_str());
}
YR_TEST(measured_sample_pdf_matches_pdfbsdf) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp2.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::ShadingMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up(); const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f,0.2f,1});
    yr::Rng rng{9u}; int checked=0;
    for (int i=0;i<2000;++i){ auto s = yr::SampleBsdf(mat, wo, n, rng.NextFloat2(), rng);
        if(!s.valid) continue;
        const float pe = yr::PdfBsdf(mat, wo, s.wi, n, rng);
        YR_EXPECT_TRUE(std::fabs(pe - s.pdf) <= 1e-2f*s.pdf + 1e-4f); ++checked; }
    YR_EXPECT_TRUE(checked>0); std::remove(p.c_str());
}
YR_TEST(measured_sample_deterministic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp3.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::ShadingMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up(); const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f,0.1f,1}); const yr::Vec2f u{0.37f,0.62f};
    yr::Rng ra{1u}, rb{1u};
    auto a = yr::SampleBsdf(mat, wo, n, u, ra); auto b = yr::SampleBsdf(mat, wo, n, u, rb);
    YR_EXPECT_EQ(a.valid,b.valid); if(a.valid){ YR_EXPECT_EQ(a.wi.x,b.wi.x); YR_EXPECT_EQ(a.pdf,b.pdf); }
    std::remove(p.c_str());
}
