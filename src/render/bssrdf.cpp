#include <yaoray/render/bssrdf.hpp>

#include <yaoray/render/catmull_rom.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

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

float BeamDiffusionMS(float sigma_s, float sigma_a, float g, float eta, float r) {
    const int nSamples = 100;
    float Ed = 0;

    // Reduced scattering coefficients and albedo.
    float sigmap_s = sigma_s * (1 - g);
    float sigmap_t = sigma_a + sigmap_s;
    float rhop = sigmap_s / sigmap_t;

    // Non-classical diffusion coefficient and effective transport coefficient.
    float D_g = (2 * sigma_a + sigmap_s) / (3 * sigmap_t * sigmap_t);
    float sigma_tr = std::sqrt(sigma_a / D_g);

    // Linear extrapolation distance and exitance scale factors.
    float fm1 = FresnelMoment1(eta), fm2 = FresnelMoment2(eta);
    float ze = -2 * D_g * (1 + 3 * fm2) / (1 - 2 * fm1);
    float cPhi = 0.25f * (1 - 2 * fm1), cE = 0.5f * (1 - 3 * fm2);

    for (int i = 0; i < nSamples; ++i) {
        // Exponential-importance-sampled real source depth.
        float zr = -std::log(1 - (i + 0.5f) / nSamples) / sigmap_t;
        float zv = -zr + 2 * ze;  // virtual (mirror) source
        float dr = std::sqrt(r * r + zr * zr);
        float dv = std::sqrt(r * r + zv * zv);

        // Dipole fluence rate and vector irradiance.
        float phiD = Inv4Pi / D_g *
                     (std::exp(-sigma_tr * dr) / dr - std::exp(-sigma_tr * dv) / dv);
        float EDn = Inv4Pi *
                    (zr * (1 + sigma_tr * dr) * std::exp(-sigma_tr * dr) / (dr * dr * dr) -
                     zv * (1 + sigma_tr * dv) * std::exp(-sigma_tr * dv) / (dv * dv * dv));

        float E = phiD * cPhi + EDn * cE;
        float kappa = 1 - std::exp(-2 * sigmap_t * (dr + zr));
        Ed += rhop * rhop * std::exp(-sigmap_t * zr) * kappa * E / nSamples;
    }
    return Ed;
}

float BeamDiffusionSS(float sigma_s, float sigma_a, float g, float eta, float r) {
    float sigma_t = sigma_a + sigma_s;
    float rho = sigma_s / sigma_t;
    float tCrit = r * SafeSqrt(1 - 1 / (eta * eta));
    float Ess = 0;
    const int nSamples = 100;
    for (int i = 0; i < nSamples; ++i) {
        float ti = tCrit - std::log(1 - (i + 0.5f) / nSamples) / sigma_t;
        float d = std::sqrt(r * r + ti * ti);
        float cosTheta_o = ti / d;
        Ess += rho * std::exp(-sigma_t * (d + ti)) / (d * d) *
               HenyeyGreenstein(cosTheta_o, g) * (1 - FrDielectric(-cosTheta_o, eta)) *
               std::abs(cosTheta_o);
    }
    return Ess / nSamples;
}

BSSRDFTable::BSSRDFTable(int n_rho_samples, int n_radius_samples)
    : n_rho(n_rho_samples),
      n_radius(n_radius_samples),
      rho_samples(n_rho_samples),
      radius_samples(n_radius_samples),
      profile((std::size_t)n_rho_samples * n_radius_samples),
      rho_eff(n_rho_samples),
      profile_cdf((std::size_t)n_rho_samples * n_radius_samples) {}

void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& t) {
    // Geometric radius discretization: 0, 2.5e-3, then *1.2 each step.
    t.radius_samples[0] = 0.0f;
    t.radius_samples[1] = 2.5e-3f;
    for (int i = 2; i < t.n_radius; ++i)
        t.radius_samples[i] = t.radius_samples[i - 1] * 1.2f;

    // Albedo discretization clustered toward rho=1.
    for (int i = 0; i < t.n_rho; ++i)
        t.rho_samples[i] = (1 - std::exp(-8.0f * i / (float)(t.n_rho - 1))) /
                           (1 - std::exp(-8.0f));

    for (int i = 0; i < t.n_rho; ++i) {
        for (int j = 0; j < t.n_radius; ++j) {
            float rho = t.rho_samples[i];
            float r = t.radius_samples[j];
            t.profile[(std::size_t)i * t.n_radius + j] =
                2 * Pi * r *
                (BeamDiffusionMS(rho, 1 - rho, g, eta, r) +
                 BeamDiffusionSS(rho, 1 - rho, g, eta, r));
        }
        // Effective albedo + radial CDF for this rho row.
        t.rho_eff[i] = IntegrateCatmullRom(
            t.n_radius, t.radius_samples.data(),
            &t.profile[(std::size_t)i * t.n_radius],
            &t.profile_cdf[(std::size_t)i * t.n_radius]);
    }
}

TabulatedBSSRDF::TabulatedBSSRDF(const Color3f& sigma_a, const Color3f& sigma_s,
                                 float eta_, const BSSRDFTable& table_)
    : eta(eta_), table(&table_) {
    sigma_t = Color3f{sigma_a.x + sigma_s.x, sigma_a.y + sigma_s.y, sigma_a.z + sigma_s.z};
    rho = Color3f{sigma_t.x != 0 ? sigma_s.x / sigma_t.x : 0.0f,
                  sigma_t.y != 0 ? sigma_s.y / sigma_t.y : 0.0f,
                  sigma_t.z != 0 ? sigma_s.z / sigma_t.z : 0.0f};
}

Color3f TabulatedBSSRDF::Sr(float r) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};
    float out[3] = {0.0f, 0.0f, 0.0f};

    for (int ch = 0; ch < 3; ++ch) {
        float rOptical = r * st[ch];

        int rhoOffset, radiusOffset;
        float rhoW[4], radiusW[4];
        if (!CatmullRomWeights(table->n_rho, table->rho_samples.data(), rh[ch], rhoOffset, rhoW) ||
            !CatmullRomWeights(table->n_radius, table->radius_samples.data(), rOptical, radiusOffset, radiusW))
            continue;

        float sr = 0;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float weight = rhoW[i] * radiusW[j];
                if (weight != 0)
                    sr += weight * table->EvalProfile(rhoOffset + i, radiusOffset + j);
            }
        }

        if (rOptical != 0) sr /= 2 * Pi * rOptical;
        out[ch] = sr;
    }

    return Color3f{std::max(0.0f, out[0] * st[0] * st[0]),
                   std::max(0.0f, out[1] * st[1] * st[1]),
                   std::max(0.0f, out[2] * st[2] * st[2])};
}

float TabulatedBSSRDF::Sw(float cos_theta) const {
    float c = 1 - 2 * FresnelMoment1(1.0f / eta);
    return (1 - FrDielectric(cos_theta, eta)) / (c * Pi);
}

Color3f TabulatedBSSRDF::S(float cos_theta_o, float r, float cos_theta_i) const {
    float ft = 1 - FrDielectric(cos_theta_o, eta);
    Color3f sp = Sp(r);
    float sw = Sw(cos_theta_i);
    return Color3f{ft * sp.x * sw, ft * sp.y * sw, ft * sp.z * sw};
}

float TabulatedBSSRDF::Sample_Sr(int ch, float u) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};
    if (st[ch] == 0) return -1;
    float r = SampleCatmullRom2D(table->n_rho, table->n_radius,
                                 table->rho_samples.data(), table->radius_samples.data(),
                                 table->profile.data(), table->profile_cdf.data(),
                                 rh[ch], u);
    return r / st[ch];
}

float TabulatedBSSRDF::Pdf_Sr(int ch, float r) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};

    float rOptical = r * st[ch];

    int rhoOffset, radiusOffset;
    float rhoW[4], radiusW[4];
    if (!CatmullRomWeights(table->n_rho, table->rho_samples.data(), rh[ch], rhoOffset, rhoW) ||
        !CatmullRomWeights(table->n_radius, table->radius_samples.data(), rOptical, radiusOffset, radiusW))
        return 0;

    float sr = 0, rhoEff = 0;
    for (int i = 0; i < 4; ++i) {
        if (rhoW[i] == 0) continue;
        rhoEff += table->rho_eff[rhoOffset + i] * rhoW[i];
        for (int j = 0; j < 4; ++j) {
            if (radiusW[j] == 0) continue;
            sr += table->EvalProfile(rhoOffset + i, radiusOffset + j) * rhoW[i] * radiusW[j];
        }
    }
    if (rOptical != 0) sr /= 2 * Pi * rOptical;
    if (rhoEff <= 0) return 0;  // degenerate (non-scattering) guard
    return std::max(0.0f, sr * st[ch] * st[ch] / rhoEff);
}

float TabulatedBSSRDF::Pdf_Sp(const Point3f& po, const Vec3f& ss, const Vec3f& ts,
                              const Vec3f& ns, const Point3f& pi, const Vec3f& ni) const {
    // Express the entry->exit offset and the exit normal in the shading frame.
    Vec3f d = po - pi;
    float dLocal[3] = {Dot(ss, d), Dot(ts, d), Dot(ns, d)};
    float nLocal[3] = {Dot(ss, ni), Dot(ts, ni), Dot(ns, ni)};

    // Radius of the offset projected into the plane perpendicular to each axis.
    float rProj[3] = {
        std::sqrt(dLocal[1] * dLocal[1] + dLocal[2] * dLocal[2]),  // ss axis
        std::sqrt(dLocal[2] * dLocal[2] + dLocal[0] * dLocal[0]),  // ts axis
        std::sqrt(dLocal[0] * dLocal[0] + dLocal[1] * dLocal[1]),  // ns axis
    };

    const float axisProb[3] = {0.25f, 0.25f, 0.5f};
    const float chProb = 1.0f / 3.0f;

    float pdf = 0;
    for (int axis = 0; axis < 3; ++axis)
        for (int ch = 0; ch < 3; ++ch)
            pdf += Pdf_Sr(ch, rProj[axis]) * std::abs(nLocal[axis]) * chProb * axisProb[axis];
    return pdf;
}

}  // namespace yr
