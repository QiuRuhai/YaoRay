#include "film_test_paths.hpp"
#include "yr_test.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include <yaoray/film/film_checkpoint.hpp>

YR_TEST(film_checkpoint_round_trips_tiny_film) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{2.0f, 4.0f, 6.0f}, 2});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 3.0f, 5.0f}, 2});
    const std::filesystem::path path = yr::test_support::FilmTestPath("roundtrip.yrcheckpoint");
    std::filesystem::remove(path);

    const yr::FilmCheckpointMetadata metadata{2, 1, 4, 2, std::uint64_t{12345}};
    const yr::FilmCheckpointWriteResult write = yr::WriteFilmCheckpoint(path, film, metadata);
    const yr::FilmCheckpointLoadResult load =
        yr::LoadFilmCheckpoint(path, 2, 1, 4, std::uint64_t{12345});

    YR_EXPECT_TRUE(write.ok);
    YR_EXPECT_TRUE(write.error.empty());
    YR_EXPECT_TRUE(load.ok);
    YR_EXPECT_TRUE(load.error.empty());
    YR_EXPECT_TRUE(load.film.has_value());
    YR_EXPECT_EQ(load.metadata.completed_spp, 2);
    YR_EXPECT_EQ(load.film->SampleCount(0, 0), 2);
    YR_EXPECT_NEAR(load.film->LinearPixel(0, 0).x, 1.0, 1e-6);
    YR_EXPECT_NEAR(load.film->LinearPixel(1, 0).z, 2.5, 1e-6);
}

YR_TEST(film_checkpoint_rejects_settings_hash_mismatch) {
    yr::Film film{1, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    const std::filesystem::path path = yr::test_support::FilmTestPath("bad_hash.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{1, 1, 2, 1, std::uint64_t{111}};

    YR_EXPECT_TRUE(yr::WriteFilmCheckpoint(path, film, metadata).ok);
    const yr::FilmCheckpointLoadResult load =
        yr::LoadFilmCheckpoint(path, 1, 1, 2, std::uint64_t{222});
    YR_EXPECT_TRUE(!load.ok);
    YR_EXPECT_TRUE(load.error.find("settings hash") != std::string::npos);
}

YR_TEST(film_checkpoint_rejects_dimension_mismatch) {
    yr::Film film{1, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    const std::filesystem::path path = yr::test_support::FilmTestPath("bad_dimensions.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{1, 1, 2, 1, std::uint64_t{111}};

    YR_EXPECT_TRUE(yr::WriteFilmCheckpoint(path, film, metadata).ok);
    const yr::FilmCheckpointLoadResult load =
        yr::LoadFilmCheckpoint(path, 2, 1, 2, std::uint64_t{111});
    YR_EXPECT_TRUE(!load.ok);
    YR_EXPECT_TRUE(load.error.find("dimensions") != std::string::npos);
}

YR_TEST(film_checkpoint_preserves_non_uniform_adaptive_samples) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 2});
    const std::filesystem::path path = yr::test_support::FilmTestPath("non_uniform.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{2, 1, 4, 2, std::uint64_t{111}};

    YR_EXPECT_TRUE(yr::WriteFilmCheckpoint(path, film, metadata).ok);
    const yr::FilmCheckpointLoadResult load =
        yr::LoadFilmCheckpoint(path, 2, 1, 4, std::uint64_t{111});
    YR_EXPECT_TRUE(load.ok);
    YR_EXPECT_EQ(load.film->SampleCount(0, 0), std::uint32_t{1});
    YR_EXPECT_EQ(load.film->SampleCount(1, 0), std::uint32_t{2});
}
