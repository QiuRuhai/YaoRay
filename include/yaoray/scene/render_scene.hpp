#pragma once

#include <memory>
#include <vector>
#include <yaoray/scene/camera.hpp>
#include <yaoray/scene/geometry.hpp>
#include <yaoray/scene/light.hpp>
#include <yaoray/scene/material.hpp>
#include <yaoray/scene/render_options.hpp>
#include <yaoray/scene/texture.hpp>

namespace yr {

struct BSSRDFTable;
struct MeasuredBrdf;

struct RenderSceneIR {
    RenderCamera camera;

    std::vector<RenderVertex>    vertices;
    std::vector<std::uint32_t>   indices;
    std::vector<RenderPrimitive> primitives;
    std::vector<RenderSphere>    spheres;
    std::vector<RenderInstance>  instances;

    std::vector<RenderMaterial> materials;
    // Heap-owned resource tables addressed by RenderMaterial indices. Surface
    // evaluation resolves those indices into short-lived ShadingMaterial views.
    std::vector<std::shared_ptr<MeasuredBrdf>> measured_brdfs;
    std::vector<std::shared_ptr<BSSRDFTable>>  bssrdf_tables;
    std::vector<RenderTexture>                 textures;

    std::vector<EmissivePrimitive>             emissive_primitives;
    RenderEnvironment                          environment;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<AnalyticLight>                 analytic_lights;
    LightSamplingCache                        light_sampling;

    GeometryView Geometry() const { return GeometryView{vertices, indices, primitives, spheres, instances}; }
};

}  // namespace yr
