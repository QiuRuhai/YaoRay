#include <yaoray/render/measured_brdf.hpp>
#include <yaoray/render/tensor_file.hpp>
#include <cstdint>
#include <string>

namespace yr {

namespace {

// Helper: require a Float32 field with specific ndim; return nullptr + set error on failure.
const TensorField* RequireFloat32(const TensorFile& tf, const char* name,
                                  int expected_ndim, std::string& error) {
    const TensorField* f = tf.Find(name);
    if (!f) {
        error = std::string("missing required field: ") + name;
        return nullptr;
    }
    if (f->dtype != TensorDType::Float32) {
        error = std::string("field '") + name + "' must be Float32";
        return nullptr;
    }
    if (static_cast<int>(f->shape.size()) != expected_ndim) {
        error = std::string("field '") + name + "' must have ndim=" +
                std::to_string(expected_ndim) + ", got " +
                std::to_string(f->shape.size());
        return nullptr;
    }
    return f;
}

} // namespace

std::optional<MeasuredBrdf> LoadMeasuredBrdf(const std::string& path, std::string& error) {
    auto tf_opt = ReadTensorFile(path, error);
    if (!tf_opt) return std::nullopt;
    const TensorFile& tf = *tf_opt;

    // --- Required Float32 1-D fields ---
    const TensorField* f_theta_i = RequireFloat32(tf, "theta_i", 1, error);
    if (!f_theta_i) return std::nullopt;

    const TensorField* f_phi_i = RequireFloat32(tf, "phi_i", 1, error);
    if (!f_phi_i) return std::nullopt;

    const TensorField* f_wavelengths = RequireFloat32(tf, "wavelengths", 1, error);
    if (!f_wavelengths) return std::nullopt;

    // --- Required Float32 2-D fields ---
    const TensorField* f_ndf = RequireFloat32(tf, "ndf", 2, error);
    if (!f_ndf) return std::nullopt;

    const TensorField* f_sigma = RequireFloat32(tf, "sigma", 2, error);
    if (!f_sigma) return std::nullopt;

    // --- Required Float32 4-D fields ---
    const TensorField* f_vndf = RequireFloat32(tf, "vndf", 4, error);
    if (!f_vndf) return std::nullopt;

    const TensorField* f_luminance = RequireFloat32(tf, "luminance", 4, error);
    if (!f_luminance) return std::nullopt;

    // --- Required Float32 5-D field ---
    const TensorField* f_spectra = RequireFloat32(tf, "spectra", 5, error);
    if (!f_spectra) return std::nullopt;

    // --- Derive dimension constants ---
    const std::uint64_t n_theta_i = f_theta_i->shape[0];
    const std::uint64_t n_phi_i   = f_phi_i->shape[0];
    const std::uint64_t n_wl      = f_wavelengths->shape[0];
    const std::uint64_t n_theta_m = f_ndf->shape[0];
    const std::uint64_t n_phi_m   = f_ndf->shape[1];

    // --- Cross-constraint: sigma must match [n_theta_m, n_phi_m] ---
    if (f_sigma->shape[0] != n_theta_m || f_sigma->shape[1] != n_phi_m) {
        error = "field 'sigma' shape must match [n_theta_m, n_phi_m] from 'ndf'";
        return std::nullopt;
    }

    // --- Cross-constraint: vndf must be [n_phi_i, n_theta_i, vndf_res, vndf_res] ---
    // The conditioning dims [0,1] tie to the incident-angle grids; the spatial
    // dims [2,3] are an independent warp resolution (often 512x512 in real .bsdf
    // files) that is NOT tied to the ndf grid.  Only assert they are positive.
    // (pbrt-v4 MeasuredBxDFData::Create imposes the same constraints.)
    if (f_vndf->shape[0] != n_phi_i || f_vndf->shape[1] != n_theta_i ||
        f_vndf->shape[2] == 0 || f_vndf->shape[3] == 0) {
        error = "field 'vndf' shape must be [n_phi_i, n_theta_i, >0, >0]";
        return std::nullopt;
    }

    // --- Cross-constraint: luminance must be [n_phi_i, n_theta_i, res, res] ---
    if (f_luminance->shape[0] != n_phi_i || f_luminance->shape[1] != n_theta_i) {
        error = "field 'luminance' shape[0..1] must match [n_phi_i, n_theta_i]";
        return std::nullopt;
    }
    if (f_luminance->shape[2] != f_luminance->shape[3]) {
        error = "field 'luminance' shape[2] must equal shape[3] (square spatial grid)";
        return std::nullopt;
    }
    const std::uint64_t res = f_luminance->shape[2];

    // --- Cross-constraint: spectra must be [n_phi_i, n_theta_i, n_wl, res, res] ---
    if (f_spectra->shape[0] != n_phi_i || f_spectra->shape[1] != n_theta_i ||
        f_spectra->shape[2] != n_wl) {
        error = "field 'spectra' shape[0..2] must match [n_phi_i, n_theta_i, n_wl]";
        return std::nullopt;
    }
    if (f_spectra->shape[3] != res || f_spectra->shape[4] != res) {
        error = "field 'spectra' shape[3..4] must match luminance spatial resolution";
        return std::nullopt;
    }

    // --- Isotropic check: n_phi_i <= 2 ---
    const bool isotropic = (n_phi_i <= 2);
    if (!isotropic) {
        error = "anisotropic measured BRDF not supported (isotropic-only)";
        return std::nullopt;
    }

    // --- Optional: jacobian ---
    bool jacobian = false;
    const TensorField* f_jacobian = tf.Find("jacobian");
    if (f_jacobian) {
        // dtype UInt8, shape [1] — read first byte
        if (f_jacobian->dtype == TensorDType::UInt8 &&
            f_jacobian->shape.size() == 1 && f_jacobian->shape[0] >= 1 &&
            !f_jacobian->data.empty()) {
            jacobian = (static_cast<unsigned char>(f_jacobian->data[0]) != 0);
        }
    }

    // --- Build result ---
    MeasuredBrdf m;
    m.isotropic = isotropic;
    m.jacobian  = jacobian;
    m.res       = static_cast<int>(res);

    m.theta_i     = f_theta_i->AsFloat32();
    m.phi_i       = f_phi_i->AsFloat32();
    m.wavelengths = f_wavelengths->AsFloat32();
    m.ndf         = f_ndf->AsFloat32();
    m.sigma       = f_sigma->AsFloat32();
    m.vndf        = f_vndf->AsFloat32();
    m.luminance   = f_luminance->AsFloat32();
    m.spectra     = f_spectra->AsFloat32();

    m.ndf_shape       = f_ndf->shape;
    m.sigma_shape     = f_sigma->shape;
    m.vndf_shape      = f_vndf->shape;
    m.luminance_shape = f_luminance->shape;
    m.spectra_shape   = f_spectra->shape;

    // --- Build the warp distributions from the raw arrays ---
    // pbrt-v4 layout: x = last grid dim, y = second-to-last grid dim. The
    // conditioning axes (phi_i, theta_i[, wavelengths]) index outer slices.
    const int n_phi_i_i   = static_cast<int>(n_phi_i);
    const int n_theta_i_i = static_cast<int>(n_theta_i);
    const int n_wl_i      = static_cast<int>(n_wl);

    // ndf / sigma: unconditioned 2D microfacet grids, no normalization/CDF.
    m.ndf_warp = PiecewiseLinear2D<0>(
        m.ndf.data(), static_cast<int>(m.ndf_shape[1]),
        static_cast<int>(m.ndf_shape[0]), {}, {}, /*normalize=*/false,
        /*build_cdf=*/false);
    m.sigma_warp = PiecewiseLinear2D<0>(
        m.sigma.data(), static_cast<int>(m.sigma_shape[1]),
        static_cast<int>(m.sigma_shape[0]), {}, {}, /*normalize=*/false,
        /*build_cdf=*/false);

    // vndf / luminance: conditioned on (phi_i, theta_i); normalize + CDFs so
    // they can be sampled/inverted.
    m.vndf_warp = PiecewiseLinear2D<2>(
        m.vndf.data(), static_cast<int>(m.vndf_shape[3]),
        static_cast<int>(m.vndf_shape[2]), {n_phi_i_i, n_theta_i_i},
        {m.phi_i.data(), m.theta_i.data()}, /*normalize=*/true,
        /*build_cdf=*/true);
    m.luminance_warp = PiecewiseLinear2D<2>(
        m.luminance.data(), static_cast<int>(m.luminance_shape[3]),
        static_cast<int>(m.luminance_shape[2]), {n_phi_i_i, n_theta_i_i},
        {m.phi_i.data(), m.theta_i.data()}, /*normalize=*/true,
        /*build_cdf=*/true);

    // spectra: conditioned on (phi_i, theta_i, wavelengths); plain bilinear
    // lookup (no normalization/CDF).
    m.spectra_warp = PiecewiseLinear2D<3>(
        m.spectra.data(), static_cast<int>(m.spectra_shape[4]),
        static_cast<int>(m.spectra_shape[3]),
        {n_phi_i_i, n_theta_i_i, n_wl_i},
        {m.phi_i.data(), m.theta_i.data(), m.wavelengths.data()},
        /*normalize=*/false, /*build_cdf=*/false);

    return m;
}

} // namespace yr
