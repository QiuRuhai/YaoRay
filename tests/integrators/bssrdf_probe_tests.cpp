#include "yr_test.hpp"
#include <yaoray/shading/bssrdf.hpp>
#include <yaoray/integrators/bssrdf_probe.hpp>
#include <yaoray/accel/acceleration.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <cmath>
#include <cstdint>

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

static yr::RenderSceneIR MakeQuadScene() {
    yr::RenderSceneIR scene;
    auto V = [&](float x, float y) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, 0.0f}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-5, -5); V(5, -5); V(5, 5); V(-5, 5);
    scene.indices = {0, 1, 2, 0, 2, 3};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 0, true, false, false});
    scene.materials.push_back(yr::RenderMaterial{});
    return scene;
}

YR_TEST(bssrdf_probe_lands_on_quad) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildRenderAcceleration(scene.Geometry());
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());

    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::BssrdfProbeSample r = yr::SampleBssrdfProbe(s, scene, built.acceleration, po, ss, ts, ns,
                                                    0, -1, -1, 0.2f, yr::Vec2f{0.5f, 0.3f});
    YR_EXPECT_TRUE(r.hit);
    YR_EXPECT_NEAR(r.pi.z, 0.0f, 1e-3f);
    YR_EXPECT_EQ(r.primitive_index, 0);
    YR_EXPECT_TRUE(r.pdf > 0.0f && std::isfinite(r.pdf));
    YR_EXPECT_TRUE(r.sp.x >= 0.0f && std::isfinite(r.sp.x));
}

YR_TEST(bssrdf_probe_pdf_matches_pdf_sp) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildRenderAcceleration(scene.Geometry());
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());

    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::BssrdfProbeSample r = yr::SampleBssrdfProbe(s, scene, built.acceleration, po, ss, ts, ns,
                                                    0, -1, -1, 0.2f, yr::Vec2f{0.6f, 0.1f});
    YR_EXPECT_TRUE(r.hit);
    float pdf_sp = s.Pdf_Sp(po, ss, ts, ns, r.pi, r.ni);
    YR_EXPECT_NEAR(r.pdf, pdf_sp, 1e-3f * pdf_sp + 1e-6f);  // nFound == 1 for a flat quad
}

YR_TEST(bssrdf_probe_deterministic) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildRenderAcceleration(scene.Geometry());
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    auto a = yr::SampleBssrdfProbe(s, scene, built.acceleration, po, ss, ts, ns, 0, -1, -1, 0.33f, yr::Vec2f{0.4f, 0.7f});
    auto b = yr::SampleBssrdfProbe(s, scene, built.acceleration, po, ss, ts, ns, 0, -1, -1, 0.33f, yr::Vec2f{0.4f, 0.7f});
    YR_EXPECT_EQ(a.hit, b.hit);
    YR_EXPECT_NEAR(a.pi.x, b.pi.x, 1e-6f);
    YR_EXPECT_NEAR(a.pdf, b.pdf, 1e-6f);
}
