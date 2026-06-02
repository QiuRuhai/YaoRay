#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <memory>
#include <cmath>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/bssrdf.hpp>

namespace {

yr::CpuPreparedScene PreparePathScene(yr::RenderSceneIR scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(std::move(scene));
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::CpuPathTraceResult RunPathTrace(yr::RenderSceneIR scene) {
    return yr::RenderCpuPathTrace(PreparePathScene(std::move(scene)));
}

yr::RenderSceneIR MakeSubsurfaceSphereScene(float sigma_a_value, float eta, bool subsurface) {
    yr::RenderSceneIR scene;
    scene.width = 16;
    scene.height = 16;
    scene.spp = 16;
    scene.max_depth = 8;
    scene.seed = 1;

    // Camera looks down -Z at the origin, matching cpu_path_tracer_tests.cpp's
    // MakeBaseScene so a unit sphere centered at the origin fills the frame center.
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.8f;

    // Uniform white environment so every escaping ray sees radiance 1. A
    // constant (untextured) environment in this codebase uses the `active=false`
    // branch of EvaluateEnvironment, which returns `radiance * strength`; setting
    // `active=true` would require a texture/distribution and evaluate to black.
    scene.environment.active = false;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.environment.strength = 1.0f;

    yr::RenderMaterial m;
    if (subsurface) {
        scene.bssrdf_tables.push_back(std::make_unique<yr::BSSRDFTable>(100, 64));
        yr::ComputeBeamDiffusionBSSRDF(0.0f, eta, *scene.bssrdf_tables.back());
        m.kind = yr::RenderMaterialKind::Subsurface;
        m.sigma_a = yr::Color3f{sigma_a_value, sigma_a_value, sigma_a_value};
        m.sigma_s = yr::Color3f{1.0f, 1.0f, 1.0f};
        m.bssrdf_eta = eta;
        m.bssrdf_table = scene.bssrdf_tables.back().get();
    } else {
        m.kind = yr::RenderMaterialKind::Diffuse;
        m.reflectance.value = yr::Color3f{0.8f, 0.8f, 0.8f};
    }
    scene.materials.push_back(m);
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{0, 0, 0}, 1.0f, 0, -1, false});
    return scene;
}

}  // namespace

// A subsurface sphere under a white environment renders to a finite, non-black,
// no-energy-gain center pixel (the integrator enters and exits the medium).
YR_TEST(cpu_subsurface_renders_finite_nonblack) {
    auto result = RunPathTrace(MakeSubsurfaceSphereScene(/*sigma_a=*/0.01f, /*eta=*/1.33f, /*subsurface=*/true));
    yr::Color3f c = result.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z));
    YR_EXPECT_TRUE(c.x > 0.0f);
    YR_EXPECT_TRUE(c.x <= 1.5f);  // no gross energy gain
}

// Determinism: same seed -> identical center pixel.
YR_TEST(cpu_subsurface_deterministic) {
    auto a = RunPathTrace(MakeSubsurfaceSphereScene(0.01f, 1.33f, true));
    auto b = RunPathTrace(MakeSubsurfaceSphereScene(0.01f, 1.33f, true));
    yr::Color3f ca = a.film.LinearPixel(8, 8), cb = b.film.LinearPixel(8, 8);
    YR_EXPECT_NEAR(ca.x, cb.x, 1e-5f);
}

// White furnace: a nearly-pure scattering medium (sigma_a = 1e-4, ~0) with a matched
// interface (eta = 1, so no Fresnel reflection or interface loss) under uniform unit
// illumination must conserve energy -- no gain. sigma_a must be non-zero to keep the
// dipole diffusion model non-degenerate (sigma_tr > 0); exact 0 makes rho_eff = 0 and
// the probe always fails. The diffusion model is approximate, so we assert the rendered
// radiance is bounded (<= 1 + tol) and non-trivial.
YR_TEST(cpu_subsurface_white_furnace_no_gain) {
    auto result = RunPathTrace(MakeSubsurfaceSphereScene(/*sigma_a=*/1e-4f, /*eta=*/1.0f, /*subsurface=*/true));
    yr::Color3f c = result.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::isfinite(c.x));
    YR_EXPECT_TRUE(c.x <= 1.05f);
    YR_EXPECT_TRUE(c.x > 0.1f);
}

YR_TEST(cpu_subsurface_differs_from_diffuse) {
    auto sss = RunPathTrace(MakeSubsurfaceSphereScene(0.02f, 1.33f, /*subsurface=*/true));
    auto diff = RunPathTrace(MakeSubsurfaceSphereScene(0.02f, 1.33f, /*subsurface=*/false));
    yr::Color3f cs = sss.film.LinearPixel(8, 8);
    yr::Color3f cd = diff.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::fabs(cs.x - cd.x) > 1e-3f);
}
