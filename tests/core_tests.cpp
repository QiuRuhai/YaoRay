#include "yr_test.hpp"

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/core/vec.hpp>

YR_TEST(vec3_adds_and_scales_components) {
    const yr::Vec3f a{1.0f, 2.0f, 3.0f};
    const yr::Vec3f b{4.0f, -1.0f, 0.5f};

    const yr::Vec3f sum = a + b;
    const yr::Vec3f scaled = 2.0f * a;

    YR_EXPECT_NEAR(sum.x, 5.0, 1e-6);
    YR_EXPECT_NEAR(sum.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(sum.z, 3.5, 1e-6);
    YR_EXPECT_NEAR(scaled.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(scaled.y, 4.0, 1e-6);
    YR_EXPECT_NEAR(scaled.z, 6.0, 1e-6);
}

YR_TEST(vec3_dot_cross_and_normalize_are_correct) {
    const yr::Vec3f x{1.0f, 0.0f, 0.0f};
    const yr::Vec3f y{0.0f, 1.0f, 0.0f};

    const yr::Vec3f z = yr::Cross(x, y);
    const yr::Vec3f n = yr::Normalize(yr::Vec3f{0.0f, 3.0f, 4.0f});

    YR_EXPECT_NEAR(yr::Dot(x, y), 0.0, 1e-6);
    YR_EXPECT_NEAR(z.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(z.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(z.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::Length(n), 1.0, 1e-6);
    YR_EXPECT_NEAR(n.y, 0.6, 1e-6);
    YR_EXPECT_NEAR(n.z, 0.8, 1e-6);
}

YR_TEST(ray_evaluates_points_along_direction) {
    const yr::Ray3f ray{yr::Point3f{1.0f, 2.0f, 3.0f}, yr::Vec3f{0.0f, 2.0f, 0.0f}};
    const yr::Point3f p = ray.At(2.5f);

    YR_EXPECT_NEAR(p.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(p.y, 7.0, 1e-6);
    YR_EXPECT_NEAR(p.z, 3.0, 1e-6);
}

YR_TEST(bounds_intersects_ray_interval) {
    const yr::Bounds3f box{yr::Point3f{-1.0f, -1.0f, -1.0f}, yr::Point3f{1.0f, 1.0f, 1.0f}};

    const yr::Ray3f hit_ray{yr::Point3f{0.0f, 0.0f, -3.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};
    const yr::Ray3f miss_ray{yr::Point3f{3.0f, 3.0f, -3.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};

    YR_EXPECT_TRUE(box.Intersects(hit_ray, 0.001f, 100.0f));
    YR_EXPECT_TRUE(!box.Intersects(miss_ray, 0.001f, 100.0f));
}
