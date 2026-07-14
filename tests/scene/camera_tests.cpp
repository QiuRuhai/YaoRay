#include "yr_test.hpp"

#include <yaoray/scene/camera.hpp>

YR_TEST(perspective_camera_center_sample_follows_forward_direction) {
    yr::RenderCamera camera;
    camera.origin = yr::Point3f{1.0f, 2.0f, 3.0f};

    const yr::Ray3f ray = yr::GeneratePerspectiveCameraRay(
        camera, 1, 1, 0, 0, yr::Vec2f{0.5f, 0.5f}
    );

    YR_EXPECT_EQ(ray.origin.x, 1.0f);
    YR_EXPECT_EQ(ray.origin.y, 2.0f);
    YR_EXPECT_EQ(ray.origin.z, 3.0f);
    YR_EXPECT_NEAR(ray.direction.x, camera.forward.x, 1.0e-6f);
    YR_EXPECT_NEAR(ray.direction.y, camera.forward.y, 1.0e-6f);
    YR_EXPECT_NEAR(ray.direction.z, camera.forward.z, 1.0e-6f);
    YR_EXPECT_TRUE(ray.has_differentials);
    YR_EXPECT_TRUE(ray.rx_direction.x > ray.direction.x);
    YR_EXPECT_TRUE(ray.ry_direction.y < ray.direction.y);
}

YR_TEST(perspective_camera_maps_image_corners_to_expected_sides) {
    yr::RenderCamera camera;

    const yr::Ray3f upper_left = yr::GeneratePerspectiveCameraRay(
        camera, 2, 2, 0, 0, yr::Vec2f{0.0f, 0.0f}
    );
    const yr::Ray3f lower_right = yr::GeneratePerspectiveCameraRay(
        camera, 2, 2, 1, 1, yr::Vec2f{1.0f, 1.0f}
    );

    YR_EXPECT_TRUE(upper_left.direction.x < 0.0f);
    YR_EXPECT_TRUE(upper_left.direction.y > 0.0f);
    YR_EXPECT_TRUE(lower_right.direction.x > 0.0f);
    YR_EXPECT_TRUE(lower_right.direction.y < 0.0f);
}
