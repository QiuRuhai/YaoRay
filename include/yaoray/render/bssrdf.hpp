#pragma once

// Subsurface scattering core — faithful port of pbrt-v4 (bssrdf.cpp + scattering.h).
// Slice 1: scattering helpers, photon beam diffusion integrands, and the
// precomputed BSSRDFTable. No integrator, no scene types — pure math.

#include <cstddef>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

// Scalar dielectric Fresnel reflectance for unpolarized light. `eta` is the
// relative IOR (transmitted / incident). cos_theta_i may be negative (light from
// the far side); eta is inverted internally in that case. Returns 1 on total
// internal reflection.
float FrDielectric(float cos_theta_i, float eta);

// Henyey-Greenstein phase function value for the angle whose cosine is cos_theta
// and asymmetry parameter g in (-1, 1). Normalizes to 1 over the sphere.
float HenyeyGreenstein(float cos_theta, float g);

// Polynomial fits to the first and second moments of the Fresnel reflectance,
// used as diffusion boundary conditions. `eta` is the relative IOR.
float FresnelMoment1(float eta);
float FresnelMoment2(float eta);

// Photon beam diffusion — multiple-scattering term. Returns the diffuse fluence
// contribution at surface radius r for a semi-infinite homogeneous medium with
// the given scattering/absorption coefficients, phase asymmetry g, and IOR eta.
float BeamDiffusionMS(float sigma_s, float sigma_a, float g, float eta, float r);

// Photon beam diffusion — single-scattering term at surface radius r.
float BeamDiffusionSS(float sigma_s, float sigma_a, float g, float eta, float r);

// Precomputed, separable diffusion profile sampled over (single-scattering albedo
// rho, optical radius r). Built once per (g, eta) by ComputeBeamDiffusionBSSRDF.
struct BSSRDFTable {
    int n_rho = 0;
    int n_radius = 0;
    std::vector<float> rho_samples;     // [n_rho]      discretized albedos
    std::vector<float> radius_samples;  // [n_radius]   discretized radii
    std::vector<float> profile;         // [n_rho*n_radius]  2*pi*r*(MS+SS)
    std::vector<float> rho_eff;         // [n_rho]      effective hemispherical albedo
    std::vector<float> profile_cdf;     // [n_rho*n_radius]  per-rho radial CDF

    BSSRDFTable(int n_rho_samples, int n_radius_samples);

    // profile[rho_index * n_radius + radius_index]
    float EvalProfile(int rho_index, int radius_index) const {
        return profile[(std::size_t)rho_index * n_radius + radius_index];
    }
};

// Fill `table.profile`, `table.rho_eff`, and `table.profile_cdf` for the given
// phase asymmetry g and relative IOR eta. Deterministic (sequential).
void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& table);

// A separable, tabulated BSSRDF instance for one shading point and medium.
// Faithful port of pbrt-v4's TabulatedBSSRDF, restricted to RGB. Constructed from
// per-channel absorption/scattering coefficients, the relative IOR, and a
// precomputed BSSRDFTable (built once per (g, eta) by ComputeBeamDiffusionBSSRDF).
// Slice 2 implements the radial profile (Sr), directional term (Sw), spatial term
// (Sp == Sr of the surface distance), the combined value (S), and 1-D radius
// importance sampling (Sample_Sr / Pdf_Sr). Exit-point sampling and scene wiring
// are later slices.
struct TabulatedBSSRDF {
    Color3f sigma_t;          // sigma_a + sigma_s, per channel
    Color3f rho;              // sigma_s / sigma_t, per channel (0 where sigma_t==0)
    float eta = 1.0f;         // relative IOR
    const BSSRDFTable* table = nullptr;

    TabulatedBSSRDF(const Color3f& sigma_a, const Color3f& sigma_s, float eta,
                    const BSSRDFTable& table);

    Color3f Sr(float r) const;
    float Sw(float cos_theta) const;
    Color3f Sp(float r) const { return Sr(r); }
    Color3f S(float cos_theta_o, float r, float cos_theta_i) const;
    float Sample_Sr(int ch, float u) const;
    float Pdf_Sr(int ch, float r) const;

    // Area-measure spatial pdf of sampling exit point `pi` (with geometric normal
    // `ni`) given entry point `po` and its orthonormal shading frame (ss, ts, ns).
    // MIS over the 3 projection axes (weights ss/ts/ns = .25/.25/.5) and the 3 RGB
    // channels (each 1/3). Mirrors SampleBssrdfProbe's axis/channel choices.
    float Pdf_Sp(const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
                 const Point3f& pi, const Vec3f& ni) const;
};

}  // namespace yr
