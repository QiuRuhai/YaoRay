#pragma once

namespace yr {

enum class TextureFilter {
    Nearest,
    Bilinear
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

} // namespace yr
