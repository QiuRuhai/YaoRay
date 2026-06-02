#include <yaoray/render/bssrdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float Inv4Pi = 0.07957747154594767f;

inline float SafeSqrt(float x) { return std::sqrt(std::max(0.0f, x)); }

}  // namespace

float FrDielectric(float cos_theta_i, float eta) {
    cos_theta_i = std::clamp(cos_theta_i, -1.0f, 1.0f);
    if (cos_theta_i < 0) {
        eta = 1.0f / eta;
        cos_theta_i = -cos_theta_i;
    }
    float sin2_theta_i = 1.0f - cos_theta_i * cos_theta_i;
    float sin2_theta_t = sin2_theta_i / (eta * eta);
    if (sin2_theta_t >= 1.0f) return 1.0f;  // total internal reflection
    float cos_theta_t = SafeSqrt(1.0f - sin2_theta_t);

    float r_parl = (eta * cos_theta_i - cos_theta_t) / (eta * cos_theta_i + cos_theta_t);
    float r_perp = (cos_theta_i - eta * cos_theta_t) / (cos_theta_i + eta * cos_theta_t);
    return 0.5f * (r_parl * r_parl + r_perp * r_perp);
}

float HenyeyGreenstein(float cos_theta, float g) {
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return Inv4Pi * (1.0f - g * g) / (denom * SafeSqrt(denom));
}

float FresnelMoment1(float eta) {
    float eta2 = eta * eta, eta3 = eta2 * eta, eta4 = eta3 * eta, eta5 = eta4 * eta;
    if (eta < 1)
        return 0.45966f - 1.73965f * eta + 3.37668f * eta2 - 3.904945f * eta3 +
               2.49277f * eta4 - 0.68441f * eta5;
    return -4.61686f + 11.1136f * eta - 10.4646f * eta2 + 5.11455f * eta3 -
           1.27198f * eta4 + 0.12746f * eta5;
}

float FresnelMoment2(float eta) {
    float eta2 = eta * eta, eta3 = eta2 * eta, eta4 = eta3 * eta, eta5 = eta4 * eta;
    if (eta < 1)
        return 0.27614f - 0.87350f * eta + 1.12077f * eta2 - 0.65095f * eta3 +
               0.07883f * eta4 + 0.04860f * eta5;
    float r_1 = -547.033f + 45.3087f / eta3 - 218.725f / eta2 + 458.843f / eta +
                404.557f * eta - 189.519f * eta2 + 54.9327f * eta3 -
                9.00603f * eta4 + 0.63942f * eta5;
    return r_1;
}

}  // namespace yr
