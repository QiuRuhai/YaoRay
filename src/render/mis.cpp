#include <yaoray/render/mis.hpp>

#include <cmath>

namespace yr {
namespace {

bool ValidEstimator(int sample_count, float pdf) {
    return sample_count > 0 && pdf > 0.0f && std::isfinite(pdf);
}

} // namespace

float PowerHeuristic(int sample_count_a, float pdf_a, int sample_count_b, float pdf_b) {
    if (!ValidEstimator(sample_count_a, pdf_a)) {
        return 0.0f;
    }
    if (!ValidEstimator(sample_count_b, pdf_b)) {
        return 1.0f;
    }

    const float a = static_cast<float>(sample_count_a) * pdf_a;
    const float b = static_cast<float>(sample_count_b) * pdf_b;
    const float a2 = a * a;
    const float b2 = b * b;
    const float denom = a2 + b2;
    if (!std::isfinite(denom) || denom <= 0.0f) {
        return 0.0f;
    }
    return a2 / denom;
}

} // namespace yr
