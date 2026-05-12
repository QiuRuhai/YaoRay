#include <yaoray/core/version.hpp>

#include <iostream>
#include <string_view>

namespace {

void PrintHelp() {
    std::cout
        << "YaoRay " << yr::VersionString() << '\n'
        << '\n'
        << "Usage:\n"
        << "  yaoray --help\n"
        << "  yaoray --version\n"
        << '\n'
        << "Renderer commands will be added after the scene and backend layers are implemented.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        PrintHelp();
        return 0;
    }

    const std::string_view arg = argv[1];
    if (arg == "--help" || arg == "-h") {
        PrintHelp();
        return 0;
    }
    if (arg == "--version") {
        std::cout << yr::VersionString() << '\n';
        return 0;
    }

    std::cerr << "Unknown argument: " << arg << '\n';
    PrintHelp();
    return 2;
}
