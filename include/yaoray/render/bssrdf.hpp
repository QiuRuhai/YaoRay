#pragma once

// Subsurface scattering core — faithful port of pbrt-v4 (bssrdf.cpp + scattering.h).
// Slice 1: scattering helpers, photon beam diffusion integrands, and the
// precomputed BSSRDFTable. No integrator, no scene types — pure math.

#include <vector>

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
};

// Fill `table.profile`, `table.rho_eff`, and `table.profile_cdf` for the given
// phase asymmetry g and relative IOR eta. Deterministic (sequential).
void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& table);

}  // namespace yr
