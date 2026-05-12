#include "yr_test.hpp"

#include <yaoray/core/version.hpp>

YR_TEST(version_string_is_present) {
    YR_EXPECT_EQ(yr::VersionString(), std::string_view{"0.1.0"});
}
