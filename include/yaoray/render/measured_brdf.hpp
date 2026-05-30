#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yr {
struct MeasuredBrdf {
    std::vector<float> theta_i, phi_i, wavelengths;
    std::vector<float> ndf, sigma, vndf, luminance, spectra;
    std::vector<std::uint64_t> ndf_shape, sigma_shape, vndf_shape, luminance_shape, spectra_shape;
    bool isotropic = false;
    bool jacobian  = false;
    int  res = 0;        // luminance_shape[2] (== spectra_shape[3]); square spatial grid
};
// nullopt + error on: read failure, missing/wrong-dtype/wrong-ndim required field, failed
// cross-constraint, OR anisotropic (n_phi_i > 2 — isotropic-only this milestone phase).
std::optional<MeasuredBrdf> LoadMeasuredBrdf(const std::string& path, std::string& error);
}  // namespace yr
