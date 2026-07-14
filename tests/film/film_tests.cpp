#include "yr_test.hpp"

#include <limits>

#include <yaoray/film/film.hpp>

YR_TEST(film_accumulates_average_radiance) {
    yr::Film film{2, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 2.0f, 3.0f});
    film.AddSample(0, 0, yr::Color3f{3.0f, 4.0f, 5.0f});

    const yr::Color3f average = film.LinearPixel(0, 0);
    YR_EXPECT_NEAR(average.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(average.y, 3.0, 1e-6);
    YR_EXPECT_NEAR(average.z, 4.0, 1e-6);
    YR_EXPECT_EQ(film.SampleCount(0, 0), 2);
}

YR_TEST(film_rejects_bad_samples) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});
    film.AddSample(0, 0, yr::Color3f{std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f});

    YR_EXPECT_NEAR(film.LinearPixel(0, 0).x, 1.0, 1e-6);
    YR_EXPECT_EQ(film.BadSampleCount(), 1);
}

YR_TEST(film_exposes_pixels_for_checkpoint_reconstruction) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{2.0f, 4.0f, 6.0f}, 2});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 3.0f, 5.0f}, 1});

    YR_EXPECT_EQ(film.Pixels().size(), std::size_t{2});
    YR_EXPECT_EQ(film.SampleCount(0, 0), 2);
    YR_EXPECT_EQ(film.SampleCount(1, 0), 1);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).x, 1.0, 1e-6);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).y, 2.0, 1e-6);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).z, 3.0, 1e-6);
}

YR_TEST(film_welford_variance_and_standard_error_are_stable) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});
    film.AddSample(0, 0, yr::Color3f{3.0f, 3.0f, 3.0f});

    YR_EXPECT_NEAR(film.LuminanceVariance(0, 0), 2.0, 1e-6);
    YR_EXPECT_NEAR(film.LuminanceStandardError(0, 0), 1.0, 1e-6);
    YR_EXPECT_EQ(film.TotalSampleCount(), std::uint64_t{2});
    YR_EXPECT_TRUE(film.IsConverged(0, 0, 2, 0.0f, 1.01f, 1.0f));
    YR_EXPECT_TRUE(!film.IsConverged(0, 0, 2, 0.0f, 0.9f, 1.0f));
}

YR_TEST(film_constant_samples_converge_at_minimum_sample_count) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{0.25f, 0.25f, 0.25f});
    YR_EXPECT_TRUE(!film.IsConverged(0, 0, 2, 0.01f, 0.0f, 1.96f));
    film.AddSample(0, 0, yr::Color3f{0.25f, 0.25f, 0.25f});
    YR_EXPECT_TRUE(film.IsConverged(0, 0, 2, 0.01f, 0.0f, 1.96f));
}

YR_TEST(film_accumulates_first_hit_aovs) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::FilmSample{
        yr::Color3f{2.0f, 3.0f, 4.0f},
        yr::Color3f{0.2f, 0.4f, 0.6f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        3.5f,
        true});
    film.AddSample(0, 0, yr::FilmSample{
        yr::Color3f{4.0f, 5.0f, 6.0f},
        yr::Color3f{0.4f, 0.6f, 0.8f},
        yr::Vec3f{0.0f, 1.0f, 1.0f},
        4.5f,
        true});

    YR_EXPECT_NEAR(film.AlbedoPixel(0, 0).x, 0.3, 1e-6);
    YR_EXPECT_NEAR(film.AlbedoPixel(0, 0).y, 0.5, 1e-6);
    YR_EXPECT_NEAR(film.DepthPixel(0, 0), 4.0, 1e-6);
    YR_EXPECT_NEAR(yr::Length(film.NormalPixel(0, 0)), 1.0, 1e-6);
}
