#include <yaoray/lighting/light_sampling.hpp>

#include <cmath>
#include <algorithm>

namespace yr {
namespace {

float DistanceSquaredToBounds(Point3f point, const Bounds3f& bounds) {
    const float dx = std::max({bounds.min.x - point.x, 0.0f, point.x - bounds.max.x});
    const float dy = std::max({bounds.min.y - point.y, 0.0f, point.y - bounds.max.y});
    const float dz = std::max({bounds.min.z - point.z, 0.0f, point.z - bounds.max.z});
    return dx * dx + dy * dy + dz * dz;
}

float TreeImportance(const LightTreeNode& node, Point3f shading_point) {
    return node.power / std::max(1.0e-4f, DistanceSquaredToBounds(shading_point, node.bounds));
}

} // namespace

AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point) {
    AnalyticLightSample sample;
    const Vec3f delta = light.position - shading_point;
    const float distance_squared = Dot(delta, delta);
    if (distance_squared <= 0.0f) return sample;

    sample.distance = std::sqrt(distance_squared);
    sample.wi = Normalize(delta);
    sample.radiance = light.intensity / distance_squared;
    sample.is_delta = true;
    sample.valid = true;
    return sample;
}

AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point) {
    (void)shading_point;
    AnalyticLightSample sample;
    const Vec3f direction_to_light = -light.direction;
    if (LengthSquared(direction_to_light) == 0.0f) return sample;

    sample.wi = Normalize(direction_to_light);
    sample.distance = 1.0e6f;
    sample.radiance = light.intensity;
    sample.is_delta = true;
    sample.valid = true;
    return sample;
}

AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point) {
    AnalyticLightSample sample;
    const Vec3f delta = light.position - shading_point;
    const float distance_squared = Dot(delta, delta);
    if (distance_squared <= 0.0f) return sample;

    const float distance = std::sqrt(distance_squared);
    const Vec3f direction_to_light = delta / distance;
    const Vec3f cone_axis = LengthSquared(light.direction) > 0.0f
        ? Normalize(light.direction)
        : Vec3f{0.0f, 0.0f, 1.0f};
    const float cone_cosine = -Dot(direction_to_light, cone_axis);

    float falloff = 0.0f;
    if (cone_cosine >= light.cone_cos_inner) {
        falloff = 1.0f;
    } else if (cone_cosine > light.cone_angle) {
        const float t = (cone_cosine - light.cone_angle) /
                        (light.cone_cos_inner - light.cone_angle);
        falloff = t * t * (3.0f - 2.0f * t);
    }

    sample.wi = direction_to_light;
    sample.distance = distance;
    sample.radiance = light.intensity * (falloff / distance_squared);
    sample.is_delta = true;
    sample.valid = true;
    return sample;
}

AnalyticLightSample SampleAnalyticLight(
    LightSceneView scene, Point3f shading_point, float select_sample) {
    if (scene.analytic_lights.empty()) return {};
    int light_index = -1;
    float selection_pdf = 1.0f;
    float sample = std::clamp(select_sample, 0.0f, std::nextafter(1.0f, 0.0f));

    const LightSamplingCache& cache = scene.sampling_cache;
    if (cache.use_analytic_tree && cache.analytic_tree_root >= 0 &&
        static_cast<std::size_t>(cache.analytic_tree_root) < cache.analytic_tree.size()) {
        int node_index = cache.analytic_tree_root;
        while (node_index >= 0) {
            const LightTreeNode& node = cache.analytic_tree[static_cast<std::size_t>(node_index)];
            if (node.light_index >= 0) {
                light_index = node.light_index;
                break;
            }
            if (node.left < 0 || node.right < 0) return {};
            const float left_weight = TreeImportance(
                cache.analytic_tree[static_cast<std::size_t>(node.left)], shading_point);
            const float right_weight = TreeImportance(
                cache.analytic_tree[static_cast<std::size_t>(node.right)], shading_point);
            const float total = left_weight + right_weight;
            const float left_probability = total > 0.0f ? left_weight / total : 0.5f;
            if (sample < left_probability) {
                selection_pdf *= left_probability;
                sample = left_probability > 0.0f ? sample / left_probability : 0.0f;
                node_index = node.left;
            } else {
                const float right_probability = 1.0f - left_probability;
                selection_pdf *= right_probability;
                sample = right_probability > 0.0f
                    ? (sample - left_probability) / right_probability : 0.0f;
                node_index = node.right;
            }
        }
    } else if (cache.analytic_power.pmf.size() == scene.analytic_lights.size()) {
        const AliasSelection selected = SampleAliasTable(cache.analytic_power, sample);
        light_index = selected.index;
        selection_pdf = selected.pmf;
    } else {
        const int count = static_cast<int>(scene.analytic_lights.size());
        light_index = std::min(static_cast<int>(sample * static_cast<float>(count)), count - 1);
        selection_pdf = 1.0f / static_cast<float>(count);
    }

    if (light_index < 0 || static_cast<std::size_t>(light_index) >= scene.analytic_lights.size() ||
        selection_pdf <= 0.0f) return {};
    const AnalyticLight& light = scene.analytic_lights[static_cast<std::size_t>(light_index)];
    AnalyticLightSample result;
    if (light.kind == AnalyticLightKind::Point) {
        result = SampleAnalyticPoint(light, shading_point);
    } else if (light.kind == AnalyticLightKind::Distant) {
        result = SampleAnalyticDistant(light, shading_point);
    } else {
        result = SampleAnalyticSpot(light, shading_point);
    }
    result.light_index = light_index;
    result.selection_pdf = selection_pdf;
    return result;
}

} // namespace yr
