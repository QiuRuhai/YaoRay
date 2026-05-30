#pragma once

// PiecewiseLinear2D - ported from pbrt-v4 (src/pbrt/util/sampling.h).
//
// A piecewise-linear 2D distribution defined on a regular grid, optionally
// conditioned on `Dimension` extra parameters (linear blend across each
// conditioning axis). This is a faithful port of pbrt-v4's class:
//   * the constructor builds the marginal/conditional CDFs and normalizes,
//   * Evaluate() does bilinear interpolation (+ param-axis blend),
//   * Invert() maps a point back to the unit square + returns the pdf.
//
// Differences from pbrt:
//   * pstd::vector/pstd::array  -> std::vector/std::array
//   * Vector2f/Point2f          -> yr::Vec2f
//   * FMA(a,b,c)                -> std::fma(a,b,c)
//   * Float                     -> float
//   * GPU/allocator machinery dropped (plain std::vector members)
//   * Sample() intentionally omitted (deferred to a later slice)
//   * Degenerate guard: when normalize is requested but the marginal total is
//     <= 0 or non-finite (e.g. an all-zero synthetic grid), normalization is
//     skipped instead of dividing by zero, so construction stays NaN/inf-free.
//
// Signature note: the conditioning parameters are passed as a variadic pack of
// numeric values (Ts... params), exactly as in pbrt. For Dimension==0 the pack
// is empty: Evaluate(pos) / Invert(sample). For Dimension==N pass N floats:
// Evaluate(pos, p0, p1, ...).

#include <yaoray/core/vec.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace yr {

template <int Dimension = 0>
class PiecewiseLinear2D {
  public:
    struct Sample {
        Vec2f p;
        float pdf;
    };

  private:
    using FloatStorage = std::vector<float>;

    // MSVC forbids zero-length C arrays; mirror pbrt's ArraySize workaround.
    static constexpr std::size_t ArraySize = (Dimension != 0) ? Dimension : 1;

    // --- small local helpers (replicating the pbrt utilities used here) ---

    static float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // pbrt's FindInterval: largest index i in [0, size-2] such that pred(i) is
    // true; clamped so the result is always a valid left interval endpoint.
    template <typename Pred>
    static std::uint32_t FindInterval(std::uint32_t size, const Pred& pred) {
        using ssize = std::int64_t;
        ssize first = 1, len = static_cast<ssize>(size) - 2;
        while (len > 0) {
            ssize half = len >> 1, middle = first + half;
            if (pred(static_cast<std::uint32_t>(middle))) {
                first = middle + 1;
                len -= half + 1;
            } else {
                len = half;
            }
        }
        ssize lo = first - 1;
        if (lo < 0) lo = 0;
        ssize hi = static_cast<ssize>(size) - 2;
        if (hi < 0) hi = 0;
        if (lo > hi) lo = hi;
        return static_cast<std::uint32_t>(lo);
    }

    static float SafeSqrt(float v) { return std::sqrt(std::max(0.f, v)); }

  public:
    PiecewiseLinear2D() = default;

    PiecewiseLinear2D(const float* data, int xSize, int ySize,
                      std::array<int, Dimension> param_res = {},
                      std::array<const float*, Dimension> param_values = {},
                      bool normalize = true, bool build_cdf = true)
        : m_size_x(xSize),
          m_size_y(ySize),
          m_patch_size_x(1.f / (xSize - 1)),
          m_patch_size_y(1.f / (ySize - 1)),
          m_inv_patch_size_x(static_cast<float>(xSize - 1)),
          m_inv_patch_size_y(static_cast<float>(ySize - 1)) {
        // build_cdf implies normalize=true in pbrt; we silently honor it.
        m_param_values.resize(ArraySize);
        for (std::size_t i = 0; i < ArraySize; ++i) {
            m_param_size[i] = 1;
            m_param_strides[i] = 0;
        }

        std::uint32_t slices = 1;
        for (int i = static_cast<int>(Dimension) - 1; i >= 0; --i) {
            // param_res must be >= 1 (caller guarantees); clamp defensively.
            int pr = param_res[i] < 1 ? 1 : param_res[i];
            m_param_size[i] = static_cast<std::uint32_t>(pr);
            m_param_values[i].resize(pr);
            if (param_values[i] != nullptr)
                std::memcpy(m_param_values[i].data(), param_values[i],
                            sizeof(float) * pr);
            m_param_strides[i] = pr > 1 ? slices : 0;
            slices *= m_param_size[i];
        }

        const std::uint32_t n_values =
            static_cast<std::uint32_t>(xSize) * static_cast<std::uint32_t>(ySize);

        m_data.assign(static_cast<std::size_t>(slices) * n_values, 0.f);

        if (build_cdf) {
            m_marginal_cdf.assign(static_cast<std::size_t>(slices) * m_size_y, 0.f);
            m_conditional_cdf.assign(static_cast<std::size_t>(slices) * n_values, 0.f);

            float* marginal_cdf = m_marginal_cdf.data();
            float* conditional_cdf = m_conditional_cdf.data();
            float* data_out = m_data.data();
            const float* data_in = data;

            for (std::uint32_t slice = 0; slice < slices; ++slice) {
                // Build the conditional CDF along x for each row.
                for (int y = 0; y < m_size_y; ++y) {
                    double sum = 0.0;
                    std::size_t i = static_cast<std::size_t>(y) * xSize;
                    conditional_cdf[i] = 0.f;
                    for (int x = 0; x < m_size_x - 1; ++x, ++i) {
                        sum += 0.5 * (static_cast<double>(data_in[i]) +
                                      static_cast<double>(data_in[i + 1]));
                        conditional_cdf[i + 1] = static_cast<float>(sum);
                    }
                }

                // Build the marginal CDF along y from the per-row totals.
                marginal_cdf[0] = 0.f;
                double sum = 0.0;
                for (int y = 0; y < m_size_y - 1; ++y) {
                    sum += 0.5 * (static_cast<double>(
                                      conditional_cdf[(y + 1) * xSize - 1]) +
                                  static_cast<double>(
                                      conditional_cdf[(y + 2) * xSize - 1]));
                    marginal_cdf[y + 1] = static_cast<float>(sum);
                }

                const float total = marginal_cdf[m_size_y - 1];
                // Degenerate guard: skip normalization on a zero/non-finite
                // marginal (e.g. an all-zero synthetic grid) to avoid 1/0.
                if (total > 0.f && std::isfinite(total)) {
                    const float normalization = 1.f / total;
                    for (std::size_t i = 0; i < n_values; ++i)
                        conditional_cdf[i] *= normalization;
                    for (int i = 0; i < m_size_y; ++i)
                        marginal_cdf[i] *= normalization;
                    for (std::size_t i = 0; i < n_values; ++i)
                        data_out[i] = data_in[i] * normalization;
                } else {
                    for (std::size_t i = 0; i < n_values; ++i)
                        data_out[i] = data_in[i];
                }

                marginal_cdf += m_size_y;
                conditional_cdf += n_values;
                data_out += n_values;
                data_in += n_values;
            }
        } else {
            float* data_out = m_data.data();
            const float* data_in = data;

            for (std::uint32_t slice = 0; slice < slices; ++slice) {
                float normalization = 1.f / HProdInvPatch();
                if (normalize) {
                    double sum = 0.0;
                    for (int y = 0; y < m_size_y - 1; ++y) {
                        std::size_t i = static_cast<std::size_t>(y) * xSize;
                        for (int x = 0; x < m_size_x - 1; ++x, ++i) {
                            float v00 = data_in[i], v10 = data_in[i + 1],
                                  v01 = data_in[i + xSize],
                                  v11 = data_in[i + 1 + xSize],
                                  avg = 0.25f * (v00 + v10 + v01 + v11);
                            sum += static_cast<double>(avg);
                        }
                    }
                    // Degenerate guard: skip normalization on a zero/non-finite
                    // total instead of dividing by zero.
                    if (sum > 0.0 && std::isfinite(sum))
                        normalization = static_cast<float>(1.0 / sum);
                    else
                        normalization = 1.f;
                }

                for (std::uint32_t k = 0; k < n_values; ++k)
                    data_out[k] = data_in[k] * normalization;

                data_in += n_values;
                data_out += n_values;
            }
        }
    }

    template <typename... Ts>
    float Evaluate(Vec2f pos, Ts... params) const {
        static_assert((std::is_arithmetic_v<Ts> && ...),
                      "Additional parameters must be numeric values");
        static_assert(sizeof...(Ts) == Dimension,
                      "Incorrect number of additional parameters passed");
        std::array<float, ArraySize> param = MakeParamArray(params...);

        float param_weight[2 * ArraySize];
        std::uint32_t slice_offset = 0u;

        if constexpr (Dimension > 0) {
            for (std::size_t dim = 0; dim < Dimension; ++dim) {
                if (m_param_size[dim] == 1) {
                    param_weight[2 * dim] = 1.f;
                    param_weight[2 * dim + 1] = 0.f;
                    continue;
                }

                std::uint32_t param_index =
                    FindInterval(m_param_size[dim], [&](std::uint32_t idx) {
                        return m_param_values[dim][idx] <= param[dim];
                    });

                float p0 = m_param_values[dim][param_index],
                      p1 = m_param_values[dim][param_index + 1];

                param_weight[2 * dim + 1] =
                    Clamp((param[dim] - p0) / (p1 - p0), 0.f, 1.f);
                param_weight[2 * dim] = 1.f - param_weight[2 * dim + 1];
                slice_offset += m_param_strides[dim] * param_index;
            }
        }

        float px = pos.x * m_inv_patch_size_x;
        float py = pos.y * m_inv_patch_size_y;
        int ox = MinI(static_cast<int>(px), m_size_x - 2);
        int oy = MinI(static_cast<int>(py), m_size_y - 2);

        float w1x = px - static_cast<float>(ox), w1y = py - static_cast<float>(oy);
        float w0x = 1.f - w1x, w0y = 1.f - w1y;

        std::uint32_t index = static_cast<std::uint32_t>(ox) +
                              static_cast<std::uint32_t>(oy) * m_size_x;

        std::uint32_t size = static_cast<std::uint32_t>(m_size_x) *
                             static_cast<std::uint32_t>(m_size_y);
        if (Dimension != 0)
            index += slice_offset * size;

        float v00 = lookup<Dimension>(m_data.data(), index, size, param_weight),
              v10 = lookup<Dimension>(m_data.data() + 1, index, size, param_weight),
              v01 = lookup<Dimension>(m_data.data() + m_size_x, index, size,
                                      param_weight),
              v11 = lookup<Dimension>(m_data.data() + m_size_x + 1, index, size,
                                      param_weight);

        return std::fma(w0y, std::fma(w0x, v00, w1x * v10),
                        w1y * std::fma(w0x, v01, w1x * v11)) *
               HProdInvPatch();
    }

    template <typename... Ts>
    Sample Invert(Vec2f sample, Ts... params) const {
        static_assert((std::is_arithmetic_v<Ts> && ...),
                      "Additional parameters must be numeric values");
        static_assert(sizeof...(Ts) == Dimension,
                      "Incorrect number of additional parameters passed");
        std::array<float, ArraySize> param = MakeParamArray(params...);

        float param_weight[2 * ArraySize];
        std::uint32_t slice_offset = 0u;

        if constexpr (Dimension > 0) {
            for (std::size_t dim = 0; dim < Dimension; ++dim) {
                if (m_param_size[dim] == 1) {
                    param_weight[2 * dim] = 1.f;
                    param_weight[2 * dim + 1] = 0.f;
                    continue;
                }

                std::uint32_t param_index =
                    FindInterval(m_param_size[dim], [&](std::uint32_t idx) {
                        return m_param_values[dim][idx] <= param[dim];
                    });

                float p0 = m_param_values[dim][param_index],
                      p1 = m_param_values[dim][param_index + 1];

                param_weight[2 * dim + 1] =
                    Clamp((param[dim] - p0) / (p1 - p0), 0.f, 1.f);
                param_weight[2 * dim] = 1.f - param_weight[2 * dim + 1];
                slice_offset += m_param_strides[dim] * param_index;
            }
        }

        float sx = sample.x * m_inv_patch_size_x;
        float sy = sample.y * m_inv_patch_size_y;
        int posx = MinI(static_cast<int>(sx), m_size_x - 2);
        int posy = MinI(static_cast<int>(sy), m_size_y - 2);
        sx -= static_cast<float>(posx);
        sy -= static_cast<float>(posy);

        std::uint32_t offset =
            static_cast<std::uint32_t>(posx) +
            static_cast<std::uint32_t>(posy) * m_size_x;
        std::uint32_t slice_size = static_cast<std::uint32_t>(m_size_x) *
                                   static_cast<std::uint32_t>(m_size_y);
        if (Dimension != 0)
            offset += slice_offset * slice_size;

        float v00 = lookup<Dimension>(m_data.data(), offset, slice_size, param_weight),
              v10 = lookup<Dimension>(m_data.data() + 1, offset, slice_size, param_weight),
              v01 = lookup<Dimension>(m_data.data() + m_size_x, offset, slice_size,
                                      param_weight),
              v11 = lookup<Dimension>(m_data.data() + m_size_x + 1, offset, slice_size,
                                      param_weight);

        float w1x = sx, w1y = sy, w0x = 1.f - w1x, w0y = 1.f - w1y;

        float c0 = std::fma(w0y, v00, w1y * v01),
              c1 = std::fma(w0y, v10, w1y * v11),
              pdf = std::fma(w0x, c0, w1x * c1);

        sx *= c0 + 0.5f * sx * (c1 - c0);

        float vc0 = lookup<Dimension>(m_conditional_cdf.data(), offset, slice_size,
                                      param_weight),
              vc1 = lookup<Dimension>(m_conditional_cdf.data() + m_size_x, offset,
                                      slice_size, param_weight);

        sx += (1.f - sy) * vc0 + sy * vc1;

        offset = static_cast<std::uint32_t>(posy) * m_size_x;
        if (Dimension != 0)
            offset += slice_offset * slice_size;

        float r0 = lookup<Dimension>(m_conditional_cdf.data(), offset + m_size_x - 1,
                                     slice_size, param_weight),
              r1 = lookup<Dimension>(m_conditional_cdf.data(),
                                     offset + (m_size_x * 2 - 1), slice_size,
                                     param_weight);

        sx /= (1.f - sy) * r0 + sy * r1;

        sy *= r0 + 0.5f * sy * (r1 - r0);

        offset = static_cast<std::uint32_t>(posy);
        if (Dimension != 0)
            offset += slice_offset * m_size_y;

        sy += lookup<Dimension>(m_marginal_cdf.data(), offset, m_size_y, param_weight);

        return Sample{Vec2f{sx, sy}, pdf * HProdInvPatch()};
    }

  private:
    template <typename... Ts>
    static std::array<float, ArraySize> MakeParamArray(Ts... params) {
        std::array<float, ArraySize> a{};
        if constexpr (sizeof...(Ts) > 0) {
            float vals[] = {static_cast<float>(params)...};
            for (std::size_t i = 0; i < sizeof...(Ts); ++i)
                a[i] = vals[i];
        }
        return a;
    }

    static int MinI(int a, int b) { return a < b ? a : b; }

    float HProdInvPatch() const { return m_inv_patch_size_x * m_inv_patch_size_y; }

    // Recursive conditioning-axis blend (pbrt's lookup<Dim>): blends slice i0
    // with the next slice along axis Dim-1, weighted by param_weight.
    template <int Dim, std::enable_if_t<Dim != 0, int> = 0>
    float lookup(const float* data, std::uint32_t i0, std::uint32_t size,
                 const float* param_weight) const {
        std::uint32_t i1 = i0 + m_param_strides[Dim - 1] * size;

        float w0 = param_weight[2 * Dim - 2], w1 = param_weight[2 * Dim - 1],
              v0 = lookup<Dim - 1>(data, i0, size, param_weight),
              v1 = lookup<Dim - 1>(data, i1, size, param_weight);

        return std::fma(v0, w0, v1 * w1);
    }

    template <int Dim, std::enable_if_t<Dim == 0, int> = 0>
    float lookup(const float* data, std::uint32_t index, std::uint32_t,
                 const float*) const {
        return data[index];
    }

    int m_size_x = 0, m_size_y = 0;
    float m_patch_size_x = 0.f, m_patch_size_y = 0.f;
    float m_inv_patch_size_x = 0.f, m_inv_patch_size_y = 0.f;
    std::uint32_t m_param_size[ArraySize] = {};
    std::uint32_t m_param_strides[ArraySize] = {};
    std::vector<FloatStorage> m_param_values;
    FloatStorage m_data;
    FloatStorage m_marginal_cdf;
    FloatStorage m_conditional_cdf;
};

}  // namespace yr
