#include <yaoray/film/denoiser.hpp>

#include <yaoray/film/film.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

#if defined(YAORAY_HAS_OIDN)
#include <OpenImageDenoise/oidn.hpp>
#endif

namespace yr {

bool IsOidnDenoiserAvailable() {
#if defined(YAORAY_HAS_OIDN)
    return true;
#else
    return false;
#endif
}

DenoiseResult DenoiseFilmWithOidn(const Film& film) {
#if !defined(YAORAY_HAS_OIDN)
    (void)film;
    return DenoiseResult{false,
        "OIDN support is not enabled; configure with -DYAORAY_ENABLE_OIDN=ON", {}};
#else
    const std::size_t pixel_count = static_cast<std::size_t>(film.Width()) *
        static_cast<std::size_t>(film.Height());
    std::vector<float> color(pixel_count * 3);
    std::vector<float> albedo(pixel_count * 3);
    std::vector<float> normal(pixel_count * 3);
    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) *
                static_cast<std::size_t>(film.Width()) + static_cast<std::size_t>(x)) * 3;
            const Color3f c = film.LinearPixel(x, y);
            const Color3f a = film.AlbedoPixel(x, y);
            const Vec3f n = film.NormalPixel(x, y);
            color[offset] = c.x;
            color[offset + 1] = c.y;
            color[offset + 2] = c.z;
            albedo[offset] = a.x;
            albedo[offset + 1] = a.y;
            albedo[offset + 2] = a.z;
            normal[offset] = n.x;
            normal[offset + 1] = n.y;
            normal[offset + 2] = n.z;
        }
    }

    oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
    device.commit();
    const std::size_t byte_count = color.size() * sizeof(float);
    oidn::BufferRef color_buffer = device.newBuffer(byte_count);
    oidn::BufferRef albedo_buffer = device.newBuffer(byte_count);
    oidn::BufferRef normal_buffer = device.newBuffer(byte_count);
    oidn::BufferRef output_buffer = device.newBuffer(byte_count);
    std::memcpy(color_buffer.getData(), color.data(), byte_count);
    std::memcpy(albedo_buffer.getData(), albedo.data(), byte_count);
    std::memcpy(normal_buffer.getData(), normal.data(), byte_count);

    oidn::FilterRef filter = device.newFilter("RT");
    filter.setImage("color", color_buffer, oidn::Format::Float3, film.Width(), film.Height());
    filter.setImage("albedo", albedo_buffer, oidn::Format::Float3, film.Width(), film.Height());
    filter.setImage("normal", normal_buffer, oidn::Format::Float3, film.Width(), film.Height());
    filter.setImage("output", output_buffer, oidn::Format::Float3, film.Width(), film.Height());
    filter.set("hdr", true);
    filter.commit();
    filter.execute();

    const char* error_message = nullptr;
    if (device.getError(error_message) != oidn::Error::None) {
        return DenoiseResult{false,
            error_message == nullptr ? "OIDN filtering failed" : error_message, {}};
    }

    const auto* output = static_cast<const float*>(output_buffer.getData());
    DenoiseResult result;
    result.ok = true;
    result.beauty.resize(pixel_count);
    for (std::size_t index = 0; index < pixel_count; ++index) {
        result.beauty[index] = Color3f{
            output[index * 3], output[index * 3 + 1], output[index * 3 + 2]};
    }
    return result;
#endif
}

} // namespace yr
