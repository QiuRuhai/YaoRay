#include "yr_test.hpp"
#include "io/tensor_test_util.hpp"
#include <yaoray/shading/measured_brdf.hpp>
#include <yaoray/shading/bsdf.hpp>
#include <yaoray/shading/shading_material.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <cmath>
#include <cstdio>

namespace { yr::Vec3f Up() { return yr::Vec3f{0,0,1}; } }

YR_TEST(measured_eval_assembly_known_value) {
    // Constant tables: ndf=N, sigma=S, spectra=P (vndf/lum positive so warps build).
    // f = P*N / (4*S*cos_wi) per RGB channel (spectra constant across wavelength).
    const float N=1.0f, S=0.5f, P=0.3f;
    const std::string p = yrtest::WriteSyntheticBsdf("measured_eval.bsdf", 2, N, S, 1.0f, 1.0f, P);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(m.has_value());
    yr::ShadingMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f,0.0f,1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.3f,0.0f,1.0f});
    yr::Rng rng{1u};
    const yr::Color3f f = yr::EvaluateBsdf(mat, wo, wi, n, rng);
    const float expect = P*N / (4.0f*S*yr::Dot(wi,n));
    YR_EXPECT_NEAR(f.x, expect, 2e-3f);
    YR_EXPECT_NEAR(f.y, expect, 2e-3f);
    YR_EXPECT_NEAR(f.z, expect, 2e-3f);
    std::remove(p.c_str());
}
YR_TEST(measured_eval_finite_deterministic_reflection_only) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_eval2.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::ShadingMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    yr::Rng ra{2u}, rb{2u};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f,0.2f,1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f,0.5f,0.9f});
    const yr::Color3f a = yr::EvaluateBsdf(mat, wo, wi, n, ra);
    const yr::Color3f b = yr::EvaluateBsdf(mat, wo, wi, n, rb);
    YR_EXPECT_TRUE(std::isfinite(a.x) && a.x >= 0.0f);
    YR_EXPECT_EQ(a.x, b.x); YR_EXPECT_EQ(a.y, b.y); YR_EXPECT_EQ(a.z, b.z);
    const yr::Color3f below = yr::EvaluateBsdf(mat, wo, yr::Normalize(yr::Vec3f{0.1f,0.1f,-1.0f}), n, ra);
    YR_EXPECT_NEAR(below.x, 0.0f, 1e-6f);
    std::remove(p.c_str());
}
