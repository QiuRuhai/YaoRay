#pragma once

#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct RenderEnvironment {
    bool active = false;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};

struct RenderEnvironmentDistribution {
    int width = 0;
    int height = 0;
    std::vector<float> texel_weights;
    std::vector<float> row_weights;
    std::vector<float> row_cdf;
    std::vector<float> conditional_cdfs;
    float total_weight = 0.0f;
    bool uniform = false;
};

struct EmissivePrimitive {
    int primitive_index = 0;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float area = 0.0f;
};

enum class AnalyticLightKind { Point, Spot, Distant };

struct AnalyticLight {
    AnalyticLightKind kind = AnalyticLightKind::Point;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    float cone_angle = 0.0f;
    float cone_cos_inner = 0.0f;
};

struct AliasTable {
    std::vector<float> probability;
    std::vector<int> alias;
    std::vector<float> pmf;
};

struct LightTreeNode {
    Bounds3f bounds;
    float power = 0.0f;
    int left = -1;
    int right = -1;
    int light_index = -1;
};

struct LightSamplingCache {
    AliasTable emissive_power;
    AliasTable analytic_power;
    std::vector<LightTreeNode> analytic_tree;
    int analytic_tree_root = -1;
    bool use_analytic_tree = false;
};

} // namespace yr
