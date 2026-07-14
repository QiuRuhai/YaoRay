#pragma once

#include <yaoray/shading/material.hpp>

namespace yr {

struct MeasuredBrdf;
struct BSSRDFTable;

struct ShadingMaterial : RenderMaterial {
    ShadingMaterial() = default;
    ShadingMaterial(const RenderMaterial& source) : RenderMaterial(source) {}

    const MeasuredBrdf* measured_brdf = nullptr;
    const BSSRDFTable* bssrdf_table = nullptr;
};

} // namespace yr
