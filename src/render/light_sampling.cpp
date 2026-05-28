#include <yaoray/render/light_sampling.hpp>

#include <cmath>
#include <cstdint>

namespace yr {

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
    const RenderPrimitive& prim = scene.primitives[ep.primitive_index];
    int tri_count = static_cast<int>(prim.index_count / 3);
    if (tri_count == 0) return std::nullopt;

    // Select a triangle within the primitive uniformly
    int selected_tri = static_cast<int>(sample_select.x * static_cast<float>(tri_count));
    if (selected_tri >= tri_count) selected_tri = tri_count - 1;

    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(selected_tri) * 3;
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
    float tri_area = Length(Cross(e1, e2)) * 0.5f;

    if (tri_area <= 0.0f || LengthSquared(normal) == 0.0f) {
        return std::nullopt;
    }

    // PDF = 1/area for uniform triangle sampling, times 1/tri_count for triangle selection
    float pdf = 1.0f / (tri_area * static_cast<float>(tri_count));

    EmissiveSample sample;
    sample.point = point;
    sample.normal = normal;
    sample.radiance = ep.radiance;
    sample.pdf = pdf;
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

    // Uniform selection among emissive primitives
    int selected = static_cast<int>(select_sample * static_cast<float>(n));
    if (selected >= n) selected = n - 1;

    auto result = SampleEmissivePrimitive(scene, selected, triangle_sample, Vec2f{select_sample, 0.0f});
    if (result.has_value()) {
        // Include 1/N selection probability
        result->pdf /= static_cast<float>(n);
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

    // Total area of all emissive primitives
    float total_area = 0.0f;
    for (const EmissivePrimitive& ep : scene.emissive_primitives) {
        total_area += ep.area;
    }
    if (total_area <= 0.0f) return 0.0f;

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

} // namespace yr
