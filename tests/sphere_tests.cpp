#include "yr_test.hpp"

#include <yaoray/core/ray.hpp>
#include <yaoray/render/shading.hpp>

YR_TEST(sphere_intersect_hits_unit_sphere_from_outside) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_NEAR(hit.t, 2.0f, 1.0e-5);
}

YR_TEST(sphere_intersect_misses_when_ray_passes_outside) {
    const yr::Ray3f ray{yr::Point3f{2.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(!hit.hit);
}

YR_TEST(sphere_intersect_picks_nearest_hit_from_inside) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_NEAR(hit.t, 1.0f, 1.0e-5);
}

YR_TEST(sphere_intersect_respects_t_max) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.5f);
    YR_EXPECT_TRUE(!hit.hit);
}

YR_TEST(sphere_bounds_contain_extents) {
    const yr::Bounds3f b = yr::SphereBounds(yr::Point3f{1.0f, 2.0f, 3.0f}, 0.5f);
    YR_EXPECT_NEAR(b.min.x, 0.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.min.y, 1.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.min.z, 2.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.x, 1.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.y, 2.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.z, 3.5f, 1.0e-6);
}

YR_TEST(sphere_normal_points_outward) {
    const yr::Vec3f n = yr::SphereNormal(yr::Point3f{0.0f, 0.0f, 0.0f}, 2.0f, yr::Point3f{2.0f, 0.0f, 0.0f});
    YR_EXPECT_NEAR(n.x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(n.y, 0.0f, 1.0e-6);
    YR_EXPECT_NEAR(n.z, 0.0f, 1.0e-6);
}

YR_TEST(sphere_uv_north_pole_is_v_one) {
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, 1.0f, 0.0f});
    YR_EXPECT_NEAR(uv.y, 1.0f, 1.0e-5);
}

YR_TEST(sphere_uv_south_pole_is_v_zero) {
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, -1.0f, 0.0f});
    YR_EXPECT_NEAR(uv.y, 0.0f, 1.0e-5);
}

YR_TEST(sphere_uv_front_seam_is_u_half) {
    // +z is the "front"; PBRT seam puts u=0.5 there.
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, 0.0f, 1.0f});
    YR_EXPECT_NEAR(uv.x, 0.5f, 1.0e-5);
}
