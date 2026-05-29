#pragma once

#include <cstdint>

#include <yaoray/core/vec.hpp>

namespace yr {

// Minimal PCG32 random-number generator. Backend-agnostic, header-only,
// no virtual dispatch (stochastic BSDF evaluation calls it in a hot loop).
// Supplies uniform random numbers to stochastic materials (the M3
// LayeredBxDF). Non-layered materials ignore it. Reference: O'Neill 2014,
// "PCG: A Family of Simple Fast Space-Efficient Statistically Good
// Algorithms for Random Number Generation."
class Rng {
public:
    explicit Rng(std::uint64_t seed) {
        state_ = 0u;
        inc_ = (seed << 1u) | 1u;
        Advance();
        state_ += seed;
        Advance();
    }

    // Uniform float in [0, 1) using the top 24 bits (float mantissa width).
    float NextFloat() {
        return static_cast<float>(Advance() >> 8) * (1.0f / 16777216.0f);
    }

    Vec2f NextFloat2() {
        const float x = NextFloat();
        const float y = NextFloat();
        return Vec2f{x, y};
    }

private:
    std::uint32_t Advance() {
        const std::uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    std::uint64_t state_ = 0u;
    std::uint64_t inc_ = 1u;
};

} // namespace yr
