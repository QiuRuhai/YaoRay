#pragma once
#include <yaoray/render/piecewise_linear_2d.hpp>

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

    // Warp distributions built from the raw arrays above (pbrt-v4 layout):
    //   x = last grid dim, y = second-to-last grid dim.
    PiecewiseLinear2D<0> ndf_warp, sigma_warp;
    PiecewiseLinear2D<2> vndf_warp, luminance_warp;
    PiecewiseLinear2D<3> spectra_warp;
};
// nullopt + error on: read failure, missing/wrong-dtype/wrong-ndim required field, failed
// cross-constraint, OR anisotropic (n_phi_i > 2 — isotropic-only this milestone phase).
std::optional<MeasuredBrdf> LoadMeasuredBrdf(const std::string& path, std::string& error);
}  // namespace yr
