#pragma once

#include <yaoray/shading/material.hpp>

namespace yrtest::bsdf {

inline bool IsBlack(yr::Color3f color) {
    return color.x == 0.0f && color.y == 0.0f && color.z == 0.0f;
}

inline yr::RenderMaterial Diffuse() {
    yr::RenderMaterial material;
    material.kind = yr::RenderMaterialKind::Diffuse;
    material.reflectance = yr::TexParam3f{{0.6f, 0.3f, 0.15f}};
    return material;
}

inline yr::RenderMaterial Conductor(float roughness = 0.0f) {
    yr::RenderMaterial material;
    material.kind = yr::RenderMaterialKind::Conductor;
    material.reflectance = yr::TexParam3f{{1.0f, 0.72f, 0.32f}};
    material.uroughness = yr::TexParam1f{roughness};
    material.vroughness = yr::TexParam1f{roughness};
    return material;
}

inline yr::RenderMaterial Dielectric(float roughness = 0.0f) {
    yr::RenderMaterial material;
    material.kind = yr::RenderMaterialKind::Dielectric;
    material.reflectance = yr::TexParam3f{{1.0f, 1.0f, 1.0f}};
    material.ior = 1.5f;
    material.uroughness = yr::TexParam1f{roughness};
    material.vroughness = yr::TexParam1f{roughness};
    return material;
}

inline yr::RenderMaterial ThinDielectric(float roughness = 0.0f) {
    yr::RenderMaterial material = Dielectric(roughness);
    material.kind = yr::RenderMaterialKind::ThinDielectric;
    return material;
}

} // namespace yrtest::bsdf
