#include "yr_test.hpp"

#include <yaoray/core/rng.hpp>

YR_TEST(rng_same_seed_produces_same_sequence) {
    yr::Rng a{42u};
    yr::Rng b{42u};
    for (int i = 0; i < 16; ++i) {
        YR_EXPECT_EQ(a.NextFloat(), b.NextFloat());
    }
}

YR_TEST(rng_values_lie_in_unit_interval) {
    yr::Rng rng{12345u};
    for (int i = 0; i < 1000; ++i) {
        const float v = rng.NextFloat();
        YR_EXPECT_TRUE(v >= 0.0f);
        YR_EXPECT_TRUE(v < 1.0f);
    }
}

YR_TEST(rng_distinct_seeds_diverge) {
    yr::Rng a{1u};
    yr::Rng b{2u};
    bool diverged = false;
    for (int i = 0; i < 8; ++i) {
        if (a.NextFloat() != b.NextFloat()) {
            diverged = true;
            break;
        }
    }
    YR_EXPECT_TRUE(diverged);
}

YR_TEST(rng_next_float2_advances_stream) {
    // NextFloat2 must draw two fresh values, equal to two NextFloat calls
    // on an identically-seeded generator.
    yr::Rng pair_rng{777u};
    yr::Rng scalar_rng{777u};
    const yr::Vec2f pair = pair_rng.NextFloat2();
    const float x = scalar_rng.NextFloat();
    const float y = scalar_rng.NextFloat();
    YR_EXPECT_EQ(pair.x, x);
    YR_EXPECT_EQ(pair.y, y);
}
