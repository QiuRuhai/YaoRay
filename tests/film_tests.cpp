#include "yr_test.hpp"

#include <limits>

#include <yaoray/film/film.hpp>
#include <yaoray/film/tone_mapping.hpp>

YR_TEST(film_accumulates_average_radiance) {
    yr::Film film{2, 1};

    film.AddSample(0, 0, yr::Color3f{1.0f, 2.0f, 3.0f});
    film.AddSample(0, 0, yr::Color3f{3.0f, 4.0f, 5.0f});

    const yr::Color3f avg = film.LinearPixel(0, 0);
    YR_EXPECT_NEAR(avg.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(avg.y, 3.0, 1e-6);
    YR_EXPECT_NEAR(avg.z, 4.0, 1e-6);
    YR_EXPECT_EQ(film.SampleCount(0, 0), 2);
}

YR_TEST(film_rejects_bad_samples) {
    yr::Film film{1, 1};

    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});
    film.AddSample(0, 0, yr::Color3f{std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f});

    const yr::Color3f avg = film.LinearPixel(0, 0);
    YR_EXPECT_NEAR(avg.x, 1.0, 1e-6);
    YR_EXPECT_EQ(film.BadSampleCount(), 1);
}

YR_TEST(tone_mapping_produces_display_range_colors) {
    const yr::ToneMapSettings settings{yr::ToneMapper::Reinhard, 0.0f};
    const yr::Color3f mapped = yr::ToDisplayColor(yr::Color3f{4.0f, 1.0f, 0.25f}, settings);

    YR_EXPECT_TRUE(mapped.x >= 0.0f && mapped.x <= 1.0f);
    YR_EXPECT_TRUE(mapped.y >= 0.0f && mapped.y <= 1.0f);
    YR_EXPECT_TRUE(mapped.z >= 0.0f && mapped.z <= 1.0f);
    YR_EXPECT_TRUE(mapped.x > mapped.y);
    YR_EXPECT_TRUE(mapped.y > mapped.z);
}
