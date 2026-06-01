#include <yaoray/render/light_sampling.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

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

float TotalEmissiveArea(const RenderSceneIR& scene) {
    float total_area = 0.0f;
    for (const EmissivePrimitive& ep : scene.emissive_primitives) {
        total_area += EmissiveArea(ep);
    }
    return total_area;
}

struct EmissiveSelection {
    int emissive_index = -1;
    float area = 0.0f;
    float local_sample = 0.0f;
};

std::optional<EmissiveSelection> SelectEmissiveByArea(
    const RenderSceneIR& scene,
    float select_sample,
    float total_area
) {
    if (!std::isfinite(total_area) || total_area <= 0.0f) {
        return std::nullopt;
    }

    const float target = ClampUnitSample(select_sample) * total_area;
    float cumulative = 0.0f;
    std::optional<EmissiveSelection> last_valid;
    for (int i = 0; i < static_cast<int>(scene.emissive_primitives.size()); ++i) {
        const float area = EmissiveArea(scene.emissive_primitives[i]);
        if (area <= 0.0f) {
            continue;
        }
        const float previous = cumulative;
        cumulative += area;
        last_valid = EmissiveSelection{i, area, 1.0f};
        if (target < cumulative) {
            return EmissiveSelection{i, area, (target - previous) / area};
        }
    }
    return last_valid;
}

struct TriangleSelection {
    int triangle_index = -1;
    float primitive_area = 0.0f;
};

std::optional<TriangleSelection> SelectTriangleByArea(
    const RenderSceneIR& scene,
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
        const Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
        const Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
        const Point3f p2 = scene.vertices[scene.indices[base + 2]].position;
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
        const Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
        const Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
        const Point3f p2 = scene.vertices[scene.indices[base + 2]].position;
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

std::optional<EmissiveSample> SampleEmissivePrimitive(
    const RenderSceneIR& scene,
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

    if (ep.primitive_index < 0 || ep.primitive_index >= static_cast<int>(scene.primitives.size())) {
        return std::nullopt;
    }

    const RenderPrimitive& prim = scene.primitives[ep.primitive_index];
    const std::optional<TriangleSelection> selected = SelectTriangleByArea(scene, prim, sample_select.x);
    if (!selected.has_value()) {
        return std::nullopt;
    }

    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(selected->triangle_index) * 3;
    Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
    Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
    Point3f p2 = scene.vertices[scene.indices[base + 2]].position;

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
    const RenderSceneIR& scene,
    float select_sample,
    Vec2f triangle_sample
) {
    int n = static_cast<int>(scene.emissive_primitives.size());
    if (n == 0) return std::nullopt;

    const float total_area = TotalEmissiveArea(scene);
    const std::optional<EmissiveSelection> selected = SelectEmissiveByArea(scene, select_sample, total_area);
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
        result->pdf *= selected->area / total_area;
    }
    return result;
}

float PdfEmissiveLightSolidAngle(
    const RenderSceneIR& scene,
    Point3f shading_point,
    Point3f light_point,
    Vec3f light_normal
) {
    int n = static_cast<int>(scene.emissive_primitives.size());
    if (n == 0) return 0.0f;

    const float total_area = TotalEmissiveArea(scene);
    if (!std::isfinite(total_area) || total_area <= 0.0f) return 0.0f;

    // Area PDF = 1 / total_area
    float area_pdf = 1.0f / total_area;

    // Convert to solid angle PDF: pdf_solid = pdf_area * dist^2 / cos_theta
    Vec3f dir = light_point - shading_point;
    float dist_sq = LengthSquared(dir);
    if (dist_sq <= 0.0f) return 0.0f;

    float cos_theta = std::fabs(Dot(light_normal, Normalize(shading_point - light_point)));
    if (cos_theta <= 0.0f) return 0.0f;

    return area_pdf * dist_sq / cos_theta;
}

AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point) {
    AnalyticLightSample s;
    const Vec3f delta{
        light.position.x - shading_point.x,
        light.position.y - shading_point.y,
        light.position.z - shading_point.z
    };
    const float dist_sq = Dot(delta, delta);
    if (dist_sq <= 0.0f) {
        return s;
    }
    s.distance = std::sqrt(dist_sq);
    s.wi = Normalize(delta);
    s.radiance = Color3f{
        light.intensity.x / dist_sq,
        light.intensity.y / dist_sq,
        light.intensity.z / dist_sq
    };
    s.is_delta = true;
    s.valid = true;
    return s;
}

AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point) {
    (void)shading_point;  // Distant lights are direction-only; position is irrelevant.
    AnalyticLightSample s;
    // The compiler stores `direction` as the propagation direction of light (from
    // the source toward the scene). At any shading point, wi points back at the
    // light source, i.e. opposite of the propagation direction.
    Vec3f wi{-light.direction.x, -light.direction.y, -light.direction.z};
    if (LengthSquared(wi) == 0.0f) {
        return s;
    }
    s.wi = Normalize(wi);
    s.distance = 1.0e6f;             // Effectively infinite for shadow-ray t_max.
    s.radiance = light.intensity;   // For a Dirac-in-direction delta, this is the
                                     // radiance directly.
    s.is_delta = true;
    s.valid = true;
    return s;
}

AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point) {
    AnalyticLightSample s;
    const Vec3f delta{
        light.position.x - shading_point.x,
        light.position.y - shading_point.y,
        light.position.z - shading_point.z
    };
    const float dist_sq = Dot(delta, delta);
    if (dist_sq <= 0.0f) {
        return s;
    }
    const float dist = std::sqrt(dist_sq);
    const Vec3f wi = Vec3f{delta.x / dist, delta.y / dist, delta.z / dist};

    // Angle between the light's cone axis (light.direction, propagating from
    // light into scene) and the direction FROM light to shading point (-wi).
    // The cosine of that angle determines the falloff.
    const Vec3f axis = LengthSquared(light.direction) > 0.0f
        ? Normalize(light.direction)
        : Vec3f{0.0f, 0.0f, 1.0f};
    const float cos_angle = -Dot(wi, axis);  // (-wi) . axis

    float falloff;
    if (cos_angle >= light.cone_cos_inner) {
        falloff = 1.0f;
    } else if (cos_angle <= light.cone_angle) {
        falloff = 0.0f;
    } else {
        // Smoothstep between cone_angle (outer cos, lower) and cone_cos_inner (higher).
        const float t = (cos_angle - light.cone_angle) /
                        (light.cone_cos_inner - light.cone_angle);
        falloff = t * t * (3.0f - 2.0f * t);
    }

    s.wi = wi;
    s.distance = dist;
    s.radiance = Color3f{
        light.intensity.x * falloff / dist_sq,
        light.intensity.y * falloff / dist_sq,
        light.intensity.z * falloff / dist_sq
    };
    s.is_delta = true;
    s.valid = true;
    return s;
}

} // namespace yr
