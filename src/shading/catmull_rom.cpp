#include <yaoray/shading/catmull_rom.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

// Binary search for the last interval whose left node satisfies pred(i)==true,
// clamped to [0, n-2]. Faithful port of pbrt's FindInterval.
template <typename Pred>
int FindInterval(int n, Pred pred) {
    int size = n - 2;
    int first = 1;
    while (size > 0) {
        int half = size >> 1;
        int middle = first + half;
        if (pred(middle)) {
            first = middle + 1;
            size -= half + 1;
        } else {
            size = half;
        }
    }
    return std::clamp(first - 1, 0, n - 2);
}

}  // namespace

bool CatmullRomWeights(int n, const float* nodes, float x, int& offset, float weights[4]) {
    if (!(x >= nodes[0] && x <= nodes[n - 1])) return false;

    int idx = FindInterval(n, [&](int i) { return nodes[i] <= x; });
    offset = idx - 1;
    float x0 = nodes[idx], x1 = nodes[idx + 1];

    float t = (x - x0) / (x1 - x0);
    float t2 = t * t, t3 = t2 * t;

    weights[1] = 2 * t3 - 3 * t2 + 1;
    weights[2] = -2 * t3 + 3 * t2;

    if (idx > 0) {
        float w0 = (t3 - 2 * t2 + t) * (x1 - x0) / (x1 - nodes[idx - 1]);
        weights[0] = -w0;
        weights[2] += w0;
    } else {
        float w0 = t3 - 2 * t2 + t;
        weights[0] = 0;
        weights[1] -= w0;
        weights[2] += w0;
    }

    if (idx + 2 < n) {
        float w3 = (t3 - t2) * (x1 - x0) / (nodes[idx + 2] - x0);
        weights[1] -= w3;
        weights[3] = w3;
    } else {
        float w3 = t3 - t2;
        weights[1] -= w3;
        weights[2] += w3;
        weights[3] = 0;
    }
    return true;
}

float IntegrateCatmullRom(int n, const float* x, const float* values, float* cdf) {
    float sum = 0;
    cdf[0] = 0;
    for (int i = 0; i < n - 1; ++i) {
        float x0 = x[i], x1 = x[i + 1];
        float f0 = values[i], f1 = values[i + 1];
        float width = x1 - x0;

        float d0, d1;
        if (i > 0)
            d0 = width * (f1 - values[i - 1]) / (x1 - x[i - 1]);
        else
            d0 = f1 - f0;
        if (i + 2 < n)
            d1 = width * (values[i + 2] - f0) / (x[i + 2] - x0);
        else
            d1 = f1 - f0;

        sum += ((d0 - d1) * (1.0f / 12.0f) + (f0 + f1) * 0.5f) * width;
        cdf[i + 1] = sum;
    }
    return sum;
}

float InvertCatmullRom(int n, const float* x, const float* values, float u) {
    if (!(u > values[0])) return x[0];
    if (!(u < values[n - 1])) return x[n - 1];

    int i = FindInterval(n, [&](int idx) { return values[idx] <= u; });
    float x0 = x[i], x1 = x[i + 1];
    float f0 = values[i], f1 = values[i + 1];
    float width = x1 - x0;

    float d0, d1;
    if (i > 0)
        d0 = width * (f1 - values[i - 1]) / (x1 - x[i - 1]);
    else
        d0 = f1 - f0;
    if (i + 2 < n)
        d1 = width * (values[i + 2] - f0) / (x[i + 2] - x0);
    else
        d1 = f1 - f0;

    float a = 0, b = 1, t = 0.5f;
    for (int iter = 0; iter < 100; ++iter) {
        if (!(t > a && t < b)) t = 0.5f * (a + b);
        float t2 = t * t, t3 = t2 * t;
        float fhat = (2 * t3 - 3 * t2 + 1) * f0 + (-2 * t3 + 3 * t2) * f1 +
                     (t3 - 2 * t2 + t) * d0 + (t3 - t2) * d1;
        float fhatDeriv = (6 * t2 - 6 * t) * f0 + (-6 * t2 + 6 * t) * f1 +
                          (3 * t2 - 4 * t + 1) * d0 + (3 * t2 - 2 * t) * d1;
        if (std::abs(fhat - u) < 1e-6f) break;
        if (fhat - u < 0)
            a = t;
        else
            b = t;
        t -= (fhat - u) / fhatDeriv;
    }
    return x0 + t * width;
}

float SampleCatmullRom2D(int size1, int size2, const float* nodes1, const float* nodes2,
                         const float* values, const float* cdf, float alpha, float u,
                         float* fval, float* pdf) {
    // Interpolation weights for the alpha parameter (selects 4 rows).
    int offset;
    float weights[4];
    if (!CatmullRomWeights(size1, nodes1, alpha, offset, weights)) return 0;

    // Blend the 4 alpha-rows at column idx. Boundary weights are 0, so
    // (offset+i) out-of-range rows are never read.
    auto interpolate = [&](const float* array, int idx) {
        float value = 0;
        for (int i = 0; i < 4; ++i)
            if (weights[i] != 0)
                value += array[(offset + i) * size2 + idx] * weights[i];
        return value;
    };

    // Map u to a spline interval by inverting the interpolated cdf.
    float maximum = interpolate(cdf, size2 - 1);
    u *= maximum;
    int idx = FindInterval(size2, [&](int i) { return interpolate(cdf, i) <= u; });

    float f0 = interpolate(values, idx), f1 = interpolate(values, idx + 1);
    float x0 = nodes2[idx], x1 = nodes2[idx + 1];
    float width = x1 - x0;

    // Re-scale u using the interpolated cdf at the interval start.
    u = (u - interpolate(cdf, idx)) / width;

    // Approximate derivatives via finite differences of the interpolant.
    float d0, d1;
    if (idx > 0)
        d0 = width * (f1 - interpolate(values, idx - 1)) / (x1 - nodes2[idx - 1]);
    else
        d0 = f1 - f0;
    if (idx + 2 < size2)
        d1 = width * (interpolate(values, idx + 2) - f0) / (nodes2[idx + 2] - x0);
    else
        d1 = f1 - f0;

    // Seed t from the quadratic approximation, then refine by Newton-bisection.
    float t = (f0 != f1) ? (f0 - std::sqrt(std::max(0.0f, f0 * f0 + 2 * u * (f1 - f0)))) / (f0 - f1)
                         : (f0 != 0 ? u / f0 : 0.0f);
    float a = 0, b = 1, Fhat, fhat;
    while (true) {
        if (!(t >= a && t <= b)) t = 0.5f * (a + b);

        Fhat = t * (f0 +
                    t * (0.5f * d0 +
                         t * ((1.0f / 3.0f) * (-2 * d0 - d1) + f1 - f0 +
                              t * (0.25f * (d0 + d1) + 0.5f * (f0 - f1)))));
        fhat = f0 +
               t * (d0 +
                    t * (-2 * d0 - d1 + 3 * (f1 - f0) +
                         t * (d0 + d1 + 2 * (f0 - f1))));

        if (std::abs(Fhat - u) < 1e-6f || b - a < 1e-6f) break;

        if (Fhat - u < 0)
            a = t;
        else
            b = t;
        t -= (Fhat - u) / fhat;
    }

    if (fval) *fval = fhat;
    if (pdf) *pdf = (maximum != 0) ? fhat / maximum : 0.0f;
    return x0 + width * t;
}

}  // namespace yr
