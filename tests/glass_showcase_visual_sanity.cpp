#include <algorithm>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: glass_showcase_visual_sanity <image.png>\n";
        return 2;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(argv[1], &width, &height, &channels, 3);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        std::cerr << "failed to load image: " << argv[1];
        if (reason != nullptr) {
            std::cerr << " (" << reason << ")";
        }
        std::cerr << '\n';
        return 1;
    }

    int near_white = 0;
    int dark = 0;
    const int total = width * height;
    for (int i = 0; i < total; ++i) {
        const unsigned char r = pixels[i * 3 + 0];
        const unsigned char g = pixels[i * 3 + 1];
        const unsigned char b = pixels[i * 3 + 2];
        if (std::min({r, g, b}) >= 245) {
            ++near_white;
        }
        if (std::max({r, g, b}) <= 50) {
            ++dark;
        }
    }
    stbi_image_free(pixels);

    const double near_white_fraction = static_cast<double>(near_white) / static_cast<double>(total);
    const double dark_fraction = static_cast<double>(dark) / static_cast<double>(total);
    std::cout << "Glass visual sanity: near_white=" << near_white_fraction
              << " dark=" << dark_fraction << '\n';

    if (near_white_fraction > 0.70) {
        std::cerr << "glass showcase is overexposed: too many near-white pixels\n";
        return 1;
    }
    if (dark_fraction < 0.03) {
        std::cerr << "glass showcase has too little dark contrast for refraction to read\n";
        return 1;
    }
    return 0;
}
