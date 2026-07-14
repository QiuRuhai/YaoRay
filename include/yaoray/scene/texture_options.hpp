#pragma once

namespace yr {

// Format-neutral texture options stored in scene and asset records. The
// shading module owns the algorithms that interpret these values.
enum class TextureFilter {
    Nearest,
    Bilinear,
    Trilinear,
    Ewa,
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat,
};

} // namespace yr
