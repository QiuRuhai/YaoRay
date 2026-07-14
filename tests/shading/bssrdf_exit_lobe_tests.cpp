#include "yr_test.hpp"
#include <yaoray/shading/bsdf.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/shading/bssrdf.hpp>
#include <yaoray/core/rng.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

static yr::RenderMaterial ExitMaterial(float eta) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::SubsurfaceExit;
    m.ior = eta;
    return m;
}

// The exit lobe is a normalized Fresnel-weighted cosine lobe: f = Sw(cos) > 0
// above the surface, pdf = cos/pi, and the sample weight is finite and ~O(1).
YR_TEST(subsurface_exit_lobe_basic) {
    yr::Rng rng{123};
    yr::RenderMaterial m = ExitMaterial(1.33f);
    yr::Vec3f n{0, 0, 1};
    yr::Vec3f wo{0, 0, 1};
    yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.3f, 0.2f, 0.9f});
    yr::Color3f f = yr::EvaluateBsdf(m, wo, wi, n, rng);
    float pdf = yr::PdfBsdf(m, wo, wi, n, rng);
    YR_EXPECT_TRUE(f.x > 0.0f && std::isfinite(f.x));
    YR_EXPECT_NEAR(pdf, std::max(0.0f, wi.z) / kPi, 1e-5f);
}

// SampleBsdf cosine-samples and returns a valid, finite, non-negative weight.
YR_TEST(subsurface_exit_lobe_sample_valid) {
    yr::Rng rng{7};
    yr::RenderMaterial m = ExitMaterial(1.33f);
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.4f, 0.6f}, rng);
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(!s.specular);
    YR_EXPECT_TRUE(s.weight.x >= 0.0f && std::isfinite(s.weight.x));
    YR_EXPECT_TRUE(s.pdf > 0.0f);
    YR_EXPECT_TRUE(s.wi.z > 0.0f);  // sampled into the upper hemisphere
}

// At eta = 1 there is no Fresnel loss, so the exit lobe is ~white: the cosine-
// sampled weight (= Sw*pi = (1-Fr)/c) is approximately 1 for any direction.
YR_TEST(subsurface_exit_lobe_white_at_eta_one) {
    yr::Rng rng{99};
    yr::RenderMaterial m = ExitMaterial(1.0f);
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.5f, 0.5f}, rng);
    YR_EXPECT_NEAR(s.weight.x, 1.0f, 0.05f);
}

// The entry kind is an inert specular interface for the generic BSDF API: delta,
// no NEE, no usable sample (the integrator handles it specially).
YR_TEST(subsurface_entry_is_delta_and_inert) {
    yr::Rng rng{1};
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::Subsurface;
    YR_EXPECT_TRUE(yr::IsDeltaBsdf(m));
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.5f, 0.5f}, rng);
    YR_EXPECT_TRUE(!s.valid);
}
