#include "yr_test.hpp"

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
