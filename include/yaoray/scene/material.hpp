#pragma once

#include <yaoray/core/vec.hpp>

namespace yr {

enum class RenderMaterialKind {
    Diffuse,
    Conductor,
    Dielectric,
    ThinDielectric,
    CoatedDiffuse,
    CoatedConductor,
    DiffuseTransmission,
    Mix,
    Measured,
    Subsurface,
    SubsurfaceExit,
};

struct TexParam1f {
    float value = 0.0f;
    int texture = -1;
};

struct TexParam3f {
    Color3f value{0.0f, 0.0f, 0.0f};
    int texture = -1;
};

struct RenderMaterial {
    RenderMaterialKind kind = RenderMaterialKind::Diffuse;
    TexParam3f reflectance{{0.5f, 0.5f, 0.5f}};
    TexParam3f eta;
    TexParam3f k;
    float ior = 1.5f;
    TexParam1f uroughness{0.0f};
    TexParam1f vroughness{0.0f};
    bool remap_roughness = true;

    int mix_material_a = -1;
    int mix_material_b = -1;
    TexParam1f mix_amount{0.5f};

    float coating_ior = 1.5f;
    TexParam1f coating_roughness{0.0f};
    float coat_thickness = 0.01f;
    Color3f coat_absorption{0.0f, 0.0f, 0.0f};
    int coat_maxdepth = 10;
    int coat_nsamples = 1;

    int measured_index = -1;

    Color3f sigma_a{0.0f, 0.0f, 0.0f};
    Color3f sigma_s{0.0f, 0.0f, 0.0f};
    float bssrdf_eta = 1.33f;
    int bssrdf_index = -1;

    int normal_map = -1;
    float normal_scale = 1.0f;
    Color3f emission{0.0f, 0.0f, 0.0f};
    TexParam1f alpha{1.0f};
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

} // namespace yr
