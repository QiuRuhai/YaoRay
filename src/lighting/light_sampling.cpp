#include <yaoray/lighting/light_sampling.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>

namespace yr {

namespace {

float ClampUnitSample(float sample) {
    if (!std::isfinite(sample)) {
        return 0.0f;
    }
    return std::clamp(sample, 0.0f, 1.0f);
}

float TriangleArea(Point3f p0, Point3f p1, Point3f p2) {
    const Vec3f e1 = p1 - p0;
    const Vec3f e2 = p2 - p0;
    const float area = Length(Cross(e1, e2)) * 0.5f;
    return std::isfinite(area) && area > 0.0f ? area : 0.0f;
}

float EmissiveArea(const EmissivePrimitive& ep) {
    return std::isfinite(ep.area) && ep.area > 0.0f ? ep.area : 0.0f;
}

float Luminance(Color3f color) {
    return std::max(0.0f, 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z);
}

float EmissivePower(const EmissivePrimitive& ep) {
    return EmissiveArea(ep) * Luminance(ep.radiance);
}

float TotalEmissiveArea(LightSceneView scene) {
    float total_area = 0.0f;
    for (const EmissivePrimitive& ep : scene.emissive_primitives) {
        total_area += EmissiveArea(ep);
    }
    return total_area;
}

float TotalEmissivePower(LightSceneView scene) {
    float total = 0.0f;
    for (const EmissivePrimitive& ep : scene.emissive_primitives) total += EmissivePower(ep);
    return total;
}

struct EmissiveSelection {
    int emissive_index = -1;
    float pmf = 0.0f;
    float local_sample = 0.0f;
};

std::optional<EmissiveSelection> SelectEmissiveByPower(
    LightSceneView scene,
    float select_sample,
    float total_power
) {
    if (scene.sampling_cache.emissive_power.pmf.size() == scene.emissive_primitives.size()) {
        const AliasSelection selected = SampleAliasTable(
            scene.sampling_cache.emissive_power, select_sample);
        if (selected.index < 0 || selected.pmf <= 0.0f) return std::nullopt;
        return EmissiveSelection{selected.index, selected.pmf, selected.remapped_sample};
    }
    if (!std::isfinite(total_power) || total_power <= 0.0f) {
        return std::nullopt;
    }

    const float target = ClampUnitSample(select_sample) * total_power;
    float cumulative = 0.0f;
    std::optional<EmissiveSelection> last_valid;
    for (int i = 0; i < static_cast<int>(scene.emissive_primitives.size()); ++i) {
        const float power = EmissivePower(scene.emissive_primitives[i]);
        if (power <= 0.0f) {
            continue;
        }
        const float previous = cumulative;
        cumulative += power;
        last_valid = EmissiveSelection{i, power / total_power, 1.0f};
        if (target < cumulative) {
            return EmissiveSelection{i, power / total_power, (target - previous) / power};
        }
    }
    return last_valid;
}

struct TriangleSelection {
    int triangle_index = -1;
    float primitive_area = 0.0f;
};

std::optional<TriangleSelection> SelectTriangleByArea(
    LightSceneView scene,
    const RenderPrimitive& prim,
    float select_sample
) {
    const int tri_count = static_cast<int>(prim.index_count / 3);
    if (tri_count == 0) {
        return std::nullopt;
    }

    float primitive_area = 0.0f;
    for (int tri = 0; tri < tri_count; ++tri) {
        const std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri) * 3;
        const Point3f p0 = scene.geometry.vertices[scene.geometry.indices[base + 0]].position;
        const Point3f p1 = scene.geometry.vertices[scene.geometry.indices[base + 1]].position;
        const Point3f p2 = scene.geometry.vertices[scene.geometry.indices[base + 2]].position;
        primitive_area += TriangleArea(p0, p1, p2);
    }
    if (!std::isfinite(primitive_area) || primitive_area <= 0.0f) {
        return std::nullopt;
    }

    const float target = ClampUnitSample(select_sample) * primitive_area;
    float cumulative = 0.0f;
    std::optional<int> last_valid;
    for (int tri = 0; tri < tri_count; ++tri) {
        const std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri) * 3;
        const Point3f p0 = scene.geometry.vertices[scene.geometry.indices[base + 0]].position;
        const Point3f p1 = scene.geometry.vertices[scene.geometry.indices[base + 1]].position;
        const Point3f p2 = scene.geometry.vertices[scene.geometry.indices[base + 2]].position;
        const float tri_area = TriangleArea(p0, p1, p2);
        if (tri_area <= 0.0f) {
            continue;
        }

        cumulative += tri_area;
        last_valid = tri;
        if (target < cumulative) {
            return TriangleSelection{tri, primitive_area};
        }
    }

    if (!last_valid.has_value()) {
        return std::nullopt;
    }
    return TriangleSelection{*last_valid, primitive_area};
}

} // namespace

AliasTable BuildAliasTable(std::span<const float> weights) {
    AliasTable table;
    const std::size_t count = weights.size();
    if (count == 0) return table;
    table.probability.assign(count, 1.0f);
    table.alias.resize(count);
    table.pmf.resize(count);
    float total = 0.0f;
    for (float weight : weights) {
        if (std::isfinite(weight) && weight > 0.0f) total += weight;
    }
    std::vector<float> scaled(count);
    std::vector<int> small;
    std::vector<int> large;
    small.reserve(count);
    large.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float weight = std::isfinite(weights[i]) && weights[i] > 0.0f ? weights[i] : 0.0f;
        table.pmf[i] = total > 0.0f ? weight / total : 1.0f / static_cast<float>(count);
        scaled[i] = table.pmf[i] * static_cast<float>(count);
        table.alias[i] = static_cast<int>(i);
        (scaled[i] < 1.0f ? small : large).push_back(static_cast<int>(i));
    }
    while (!small.empty() && !large.empty()) {
        const int low = small.back();
        small.pop_back();
        const int high = large.back();
        large.pop_back();
        table.probability[static_cast<std::size_t>(low)] = scaled[static_cast<std::size_t>(low)];
        table.alias[static_cast<std::size_t>(low)] = high;
        scaled[static_cast<std::size_t>(high)] =
            scaled[static_cast<std::size_t>(high)] + scaled[static_cast<std::size_t>(low)] - 1.0f;
        (scaled[static_cast<std::size_t>(high)] < 1.0f ? small : large).push_back(high);
    }
    return table;
}

AliasSelection SampleAliasTable(const AliasTable& table, float sample) {
    const int count = static_cast<int>(table.pmf.size());
    if (count == 0 || table.probability.size() != table.pmf.size() ||
        table.alias.size() != table.pmf.size()) return {};
    const float scaled = std::min(ClampUnitSample(sample),
        std::nextafter(1.0f, 0.0f)) * static_cast<float>(count);
    const int column = std::min(static_cast<int>(scaled), count - 1);
    const float fraction = scaled - static_cast<float>(column);
    const float probability = table.probability[static_cast<std::size_t>(column)];
    int index = column;
    float remapped = 0.0f;
    if (fraction < probability) {
        remapped = probability > 0.0f ? fraction / probability : 0.0f;
    } else {
        index = table.alias[static_cast<std::size_t>(column)];
        remapped = probability < 1.0f ? (fraction - probability) / (1.0f - probability) : 0.0f;
    }
    return AliasSelection{index, table.pmf[static_cast<std::size_t>(index)],
        std::clamp(remapped, 0.0f, std::nextafter(1.0f, 0.0f))};
}

void PrepareLightSampling(RenderSceneIR& scene) {
    std::vector<float> emissive_weights;
    emissive_weights.reserve(scene.emissive_primitives.size());
    for (const EmissivePrimitive& light : scene.emissive_primitives) {
        emissive_weights.push_back(EmissivePower(light));
    }
    scene.light_sampling.emissive_power = BuildAliasTable(emissive_weights);

    std::vector<float> analytic_weights;
    analytic_weights.reserve(scene.analytic_lights.size());
    std::vector<int> local_indices;
    local_indices.reserve(scene.analytic_lights.size());
    for (int index = 0; index < static_cast<int>(scene.analytic_lights.size()); ++index) {
        const AnalyticLight& light = scene.analytic_lights[static_cast<std::size_t>(index)];
        float power = Luminance(light.intensity);
        if (light.kind == AnalyticLightKind::Spot) {
            power *= std::max(0.0f, 2.0f * 3.14159265358979323846f * (1.0f - light.cone_angle));
        } else if (light.kind == AnalyticLightKind::Point) {
            power *= 4.0f * 3.14159265358979323846f;
        }
        analytic_weights.push_back(power);
        if (light.kind != AnalyticLightKind::Distant && power > 0.0f) local_indices.push_back(index);
    }
    scene.light_sampling.analytic_power = BuildAliasTable(analytic_weights);
    scene.light_sampling.analytic_tree.clear();
    scene.light_sampling.analytic_tree_root = -1;
    scene.light_sampling.use_analytic_tree =
        local_indices.size() == scene.analytic_lights.size() && local_indices.size() > 8;
    if (!scene.light_sampling.use_analytic_tree) return;

    auto& nodes = scene.light_sampling.analytic_tree;
    nodes.reserve(local_indices.size() * 2);
    std::function<int(std::size_t, std::size_t)> build = [&](std::size_t begin, std::size_t end) {
        Bounds3f bounds;
        Bounds3f centroid_bounds;
        float power = 0.0f;
        for (std::size_t i = begin; i < end; ++i) {
            const int light_index = local_indices[i];
            const Point3f position = scene.analytic_lights[static_cast<std::size_t>(light_index)].position;
            bounds = Union(bounds, position);
            centroid_bounds = Union(centroid_bounds, position);
            power += analytic_weights[static_cast<std::size_t>(light_index)];
        }
        const int node_index = static_cast<int>(nodes.size());
        nodes.push_back(LightTreeNode{bounds, power});
        if (end - begin == 1) {
            nodes[static_cast<std::size_t>(node_index)].light_index = local_indices[begin];
            return node_index;
        }
        const Vec3f extent = centroid_bounds.max - centroid_bounds.min;
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const std::size_t middle = begin + (end - begin) / 2;
        const auto coordinate = [&](int light_index) {
            const Point3f p = scene.analytic_lights[static_cast<std::size_t>(light_index)].position;
            return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
        };
        std::nth_element(local_indices.begin() + static_cast<std::ptrdiff_t>(begin),
            local_indices.begin() + static_cast<std::ptrdiff_t>(middle),
            local_indices.begin() + static_cast<std::ptrdiff_t>(end),
            [&](int a, int b) { return coordinate(a) < coordinate(b); });
        const int left = build(begin, middle);
        const int right = build(middle, end);
        nodes[static_cast<std::size_t>(node_index)].left = left;
        nodes[static_cast<std::size_t>(node_index)].right = right;
        return node_index;
    };
    scene.light_sampling.analytic_tree_root = build(0, local_indices.size());
}

std::optional<EmissiveSample> SampleEmissivePrimitive(
    LightSceneView scene,
    int emissive_index,
    Vec2f sample_triangle,
    Vec2f sample_select
) {
    if (emissive_index < 0 || emissive_index >= static_cast<int>(scene.emissive_primitives.size())) {
        return std::nullopt;
    }

    const EmissivePrimitive& ep = scene.emissive_primitives[emissive_index];
    const float primitive_area = EmissiveArea(ep);
    if (primitive_area <= 0.0f) {
        return std::nullopt;
    }

    if (ep.primitive_index < 0 ||
        ep.primitive_index >= static_cast<int>(scene.geometry.primitives.size())) {
        return std::nullopt;
    }

    const RenderPrimitive& prim = scene.geometry.primitives[static_cast<std::size_t>(ep.primitive_index)];
    const std::optional<TriangleSelection> selected = SelectTriangleByArea(scene, prim, sample_select.x);
    if (!selected.has_value()) {
        return std::nullopt;
    }

    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(selected->triangle_index) * 3;
    Point3f p0 = scene.geometry.vertices[scene.geometry.indices[base + 0]].position;
    Point3f p1 = scene.geometry.vertices[scene.geometry.indices[base + 1]].position;
    Point3f p2 = scene.geometry.vertices[scene.geometry.indices[base + 2]].position;

    // Square-to-triangle mapping
    float su = std::sqrt(sample_triangle.x);
    float u = 1.0f - su;
    float v = sample_triangle.y * su;
    Point3f point = p0 * (1.0f - u - v) + p1 * u + p2 * v;

    Vec3f e1 = p1 - p0;
    Vec3f e2 = p2 - p0;
    Vec3f normal = Normalize(Cross(e1, e2));
    float tri_area = TriangleArea(p0, p1, p2);

    if (tri_area <= 0.0f || LengthSquared(normal) == 0.0f) {
        return std::nullopt;
    }

    EmissiveSample sample;
    sample.point = point;
    sample.normal = normal;
    sample.radiance = ep.radiance;
    sample.pdf = 1.0f / primitive_area;
    sample.emissive_index = emissive_index;
    return sample;
}

std::optional<EmissiveSample> SampleEmissiveLights(
    LightSceneView scene,
    float select_sample,
    Vec2f triangle_sample
) {
    int n = static_cast<int>(scene.emissive_primitives.size());
    if (n == 0) return std::nullopt;

    const float total_power = TotalEmissivePower(scene);
    const std::optional<EmissiveSelection> selected = SelectEmissiveByPower(scene, select_sample, total_power);
    if (!selected.has_value()) {
        return std::nullopt;
    }

    auto result = SampleEmissivePrimitive(
        scene,
        selected->emissive_index,
        triangle_sample,
        Vec2f{selected->local_sample, 0.0f}
    );
    if (result.has_value()) {
        result->pdf *= selected->pmf;
    }
    return result;
}

float PdfEmissiveLightSolidAngle(
    LightSceneView scene,
    Point3f shading_point,
    Point3f light_point,
    Vec3f light_normal,
    int emissive_index
) {
    int n = static_cast<int>(scene.emissive_primitives.size());
    if (n == 0) return 0.0f;

    float area_pdf = 0.0f;
    if (emissive_index >= 0 && emissive_index < n) {
        const float area = EmissiveArea(scene.emissive_primitives[static_cast<std::size_t>(emissive_index)]);
        if (area <= 0.0f) return 0.0f;
        float pmf = 0.0f;
        if (scene.sampling_cache.emissive_power.pmf.size() == scene.emissive_primitives.size()) {
            pmf = scene.sampling_cache.emissive_power.pmf[static_cast<std::size_t>(emissive_index)];
        } else {
            const float total_power = TotalEmissivePower(scene);
            pmf = total_power > 0.0f
                ? EmissivePower(scene.emissive_primitives[static_cast<std::size_t>(emissive_index)]) / total_power
                : 0.0f;
        }
        area_pdf = pmf / area;
    } else {
        const float total_area = TotalEmissiveArea(scene);
        if (!std::isfinite(total_area) || total_area <= 0.0f) return 0.0f;
        area_pdf = 1.0f / total_area;
    }

    // Convert to solid angle PDF: pdf_solid = pdf_area * dist^2 / cos_theta
    Vec3f dir = light_point - shading_point;
    float dist_sq = LengthSquared(dir);
    if (dist_sq <= 0.0f) return 0.0f;

    float cos_theta = std::fabs(Dot(light_normal, Normalize(shading_point - light_point)));
    if (cos_theta <= 0.0f) return 0.0f;

    return area_pdf * dist_sq / cos_theta;
}

} // namespace yr
