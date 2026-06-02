#include <yaoray/render/catmull_rom.hpp>

#include <algorithm>

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

}  // namespace yr
