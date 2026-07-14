#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace yr::test_support {

inline std::filesystem::path FilmTestPath(std::string_view name) {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "yaoray_film_tests";
    std::filesystem::create_directories(directory);
    return directory / std::string{name};
}

} // namespace yr::test_support
