#include "yr_test.hpp"

#include <cmath>
#include <limits>
#include <optional>

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Rewrite light sampling tests for emissive primitive API.

namespace {

yr::RenderSceneIR MakeEmissiveScene() {
    yr::RenderSceneIR scene;
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f,  1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    yr::RenderMaterial mat;
    mat.emission = yr::Color3f{3.0f, 2.0f, 1.0f};
    scene.materials.push_back(mat);
    scene.emissive_primitives.push_back(yr::EmissivePrimitive{0, yr::Color3f{3.0f, 2.0f, 1.0f}, 2.0f});
    return scene;
}

yr::RenderSceneIR MakeTwoAreaEmissiveScene() {
    yr::RenderSceneIR scene;
    scene.vertices = {
        // Triangle 0: area = 2, normal points downward.
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f,  1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        // Triangle 1: area = 8, normal points downward.
        yr::RenderVertex{yr::Point3f{-2.0f, 3.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 3.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-2.0f, 3.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2, 3, 4, 5};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    scene.primitives.push_back(yr::RenderPrimitive{3, 3, 0, true, false, false});

    yr::RenderMaterial mat;
    mat.emission = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials.push_back(mat);

    scene.emissive_primitives.push_back(yr::EmissivePrimitive{0, yr::Color3f{1.0f, 1.0f, 1.0f}, 2.0f});
    scene.emissive_primitives.push_back(yr::EmissivePrimitive{1, yr::Color3f{1.0f, 1.0f, 1.0f}, 8.0f});
    return scene;
}

yr::RenderSceneIR MakeTwoTriangleEmissivePrimitiveScene() {
    yr::RenderSceneIR scene;
    scene.vertices = {
        // Triangle 0: area = 2, normal points downward.
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f,  1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        // Triangle 1: area = 8, normal points downward.
        yr::RenderVertex{yr::Point3f{-2.0f, 3.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 3.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-2.0f, 3.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2, 3, 4, 5};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 0, true, false, false});

    yr::RenderMaterial mat;
    mat.emission = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials.push_back(mat);
    scene.emissive_primitives.push_back(yr::EmissivePrimitive{0, yr::Color3f{1.0f, 1.0f, 1.0f}, 10.0f});
    return scene;
}

yr::RenderSceneIR MakeStoredAreaMismatchEmissiveScene() {
    yr::RenderSceneIR scene = MakeEmissiveScene();
    scene.emissive_primitives[0].area = 8.0f;
    return scene;
}

} // namespace

YR_TEST(light_sampling_emissive_returns_valid_sample) {
    const yr::RenderSceneIR scene = MakeEmissiveScene();

    const std::optional<yr::EmissiveSample> sample = yr::SampleEmissiveLights(
        scene, 0.5f, yr::Vec2f{0.3f, 0.3f});

    YR_EXPECT_TRUE(sample.has_value());
    if (sample.has_value()) {
        YR_EXPECT_TRUE(sample->pdf > 0.0f);
        YR_EXPECT_TRUE(sample->radiance.x > 0.0f);
    }
}

YR_TEST(light_sampling_emissive_selects_primitives_by_area) {
    const yr::RenderSceneIR scene = MakeTwoAreaEmissiveScene();

    const std::optional<yr::EmissiveSample> first = yr::SampleEmissiveLights(
        scene, 0.19f, yr::Vec2f{0.25f, 0.50f});
    const std::optional<yr::EmissiveSample> boundary = yr::SampleEmissiveLights(
        scene, 0.20f, yr::Vec2f{0.25f, 0.50f});
    const std::optional<yr::EmissiveSample> second = yr::SampleEmissiveLights(
        scene, 0.21f, yr::Vec2f{0.25f, 0.50f});

    YR_EXPECT_TRUE(first.has_value());
    YR_EXPECT_TRUE(boundary.has_value());
    YR_EXPECT_TRUE(second.has_value());
    if (!first.has_value() || !boundary.has_value() || !second.has_value()) {
        return;
    }

    // Total emissive area is 10. The first primitive covers [0, 0.2);
    // the second covers [0.2, 1].
    YR_EXPECT_EQ(first->emissive_index, 0);
    YR_EXPECT_EQ(boundary->emissive_index, 1);
    YR_EXPECT_EQ(second->emissive_index, 1);
}

YR_TEST(light_sampling_emissive_primitive_rejects_invalid_stored_area) {
    const float invalid_areas[] = {
        0.0f,
        -1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
    };

    for (float invalid_area : invalid_areas) {
        yr::RenderSceneIR scene = MakeEmissiveScene();
        scene.emissive_primitives[0].area = invalid_area;

        const std::optional<yr::EmissiveSample> sample = yr::SampleEmissivePrimitive(
            scene, 0, yr::Vec2f{0.25f, 0.50f}, yr::Vec2f{0.25f, 0.0f});
        YR_EXPECT_TRUE(!sample.has_value());
    }
}

YR_TEST(light_sampling_emissive_primitive_selects_internal_triangles_by_area) {
    const yr::RenderSceneIR scene = MakeTwoTriangleEmissivePrimitiveScene();

    const std::optional<yr::EmissiveSample> first = yr::SampleEmissivePrimitive(
        scene, 0, yr::Vec2f{0.25f, 0.50f}, yr::Vec2f{0.19f, 0.0f});
    const std::optional<yr::EmissiveSample> second = yr::SampleEmissivePrimitive(
        scene, 0, yr::Vec2f{0.25f, 0.50f}, yr::Vec2f{0.21f, 0.0f});

    YR_EXPECT_TRUE(first.has_value());
    YR_EXPECT_TRUE(second.has_value());
    if (!first.has_value() || !second.has_value()) {
        return;
    }

    YR_EXPECT_NEAR(first->point.y, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(second->point.y, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(first->pdf, 0.1f, 1.0e-6);
    YR_EXPECT_NEAR(second->pdf, 0.1f, 1.0e-6);
}

YR_TEST(light_sampling_emissive_primitive_pdf_uses_stored_area) {
    const yr::RenderSceneIR scene = MakeStoredAreaMismatchEmissiveScene();

    const std::optional<yr::EmissiveSample> sample = yr::SampleEmissivePrimitive(
        scene, 0, yr::Vec2f{0.25f, 0.50f}, yr::Vec2f{0.25f, 0.0f});

    YR_EXPECT_TRUE(sample.has_value());
    if (!sample.has_value()) {
        return;
    }

    YR_EXPECT_NEAR(sample->pdf, 1.0f / 8.0f, 1.0e-6);
}

YR_TEST(light_sampling_emissive_sample_pdf_matches_total_area_distribution) {
    const yr::RenderSceneIR scene = MakeTwoAreaEmissiveScene();
    const yr::Point3f shading_point{0.0f, 0.0f, 0.0f};

    const std::optional<yr::EmissiveSample> sample = yr::SampleEmissiveLights(
        scene, 0.75f, yr::Vec2f{0.25f, 0.50f});

    YR_EXPECT_TRUE(sample.has_value());
    if (!sample.has_value()) {
        return;
    }

    const float expected_area_pdf = 1.0f / 10.0f;
    YR_EXPECT_NEAR(sample->pdf, expected_area_pdf, 1.0e-6);

    const yr::Vec3f to_light = sample->point - shading_point;
    const float dist_sq = yr::LengthSquared(to_light);
    const float cos_light = std::fabs(yr::Dot(sample->normal, yr::Normalize(shading_point - sample->point)));
    const float expected_solid_pdf = expected_area_pdf * dist_sq / cos_light;

    const float queried_pdf = yr::PdfEmissiveLightSolidAngle(
        scene, shading_point, sample->point, sample->normal);
    YR_EXPECT_NEAR(queried_pdf, expected_solid_pdf, 1.0e-5);
}

YR_TEST(light_sampling_emissive_sample_pdf_matches_stored_area_distribution) {
    const yr::RenderSceneIR scene = MakeStoredAreaMismatchEmissiveScene();
    const yr::Point3f shading_point{0.0f, 0.0f, 0.0f};

    const std::optional<yr::EmissiveSample> sample = yr::SampleEmissiveLights(
        scene, 0.50f, yr::Vec2f{0.25f, 0.50f});

    YR_EXPECT_TRUE(sample.has_value());
    if (!sample.has_value()) {
        return;
    }

    const float expected_area_pdf = 1.0f / 8.0f;
    YR_EXPECT_NEAR(sample->pdf, expected_area_pdf, 1.0e-6);

    const yr::Vec3f to_light = sample->point - shading_point;
    const float dist_sq = yr::LengthSquared(to_light);
    const float cos_light = std::fabs(yr::Dot(sample->normal, yr::Normalize(shading_point - sample->point)));
    const float expected_solid_pdf = expected_area_pdf * dist_sq / cos_light;

    const float queried_pdf = yr::PdfEmissiveLightSolidAngle(
        scene, shading_point, sample->point, sample->normal);
    YR_EXPECT_NEAR(queried_pdf, expected_solid_pdf, 1.0e-5);
}
