#include "yr_test.hpp"

#include <yaoray/integrators/mis.hpp>

YR_TEST(mis_power_heuristic_balances_equal_estimators) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, 1.0f), 0.5, 1e-6);
}

YR_TEST(mis_power_heuristic_favors_larger_pdf) {
    const float weight = yr::PowerHeuristic(1, 4.0f, 1, 1.0f);

    YR_EXPECT_NEAR(weight, 16.0 / 17.0, 1e-6);
}

YR_TEST(mis_power_heuristic_accounts_for_sample_counts) {
    const float weight = yr::PowerHeuristic(4, 1.0f, 1, 1.0f);

    YR_EXPECT_NEAR(weight, 16.0 / 17.0, 1e-6);
}

YR_TEST(mis_power_heuristic_rejects_invalid_a_estimator) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(0, 1.0f, 1, 1.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 0.0f, 1, 1.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, -1.0f, 1, 1.0f), 0.0, 1e-6);
}

YR_TEST(mis_power_heuristic_uses_full_weight_when_b_cannot_compete) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 0, 1.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, 0.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, -1.0f), 1.0, 1e-6);
}
