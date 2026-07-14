#include <yaoray/integrators/bssrdf_probe.hpp>

#include <yaoray/accel/acceleration.hpp>
#include <yaoray/geometry/intersection.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

}  // namespace

BssrdfProbeSample SampleBssrdfProbe(
    const TabulatedBSSRDF& bssrdf,
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
    int target_primitive_index, int target_instance_index, int target_sphere_index,
    float u1, const Vec2f& u2) {
    BssrdfProbeSample out;

    Vec3f vx, vy, vz;
    if (u1 < 0.5f) {
        vx = ss; vy = ts; vz = ns;
        u1 *= 2.0f;
    } else if (u1 < 0.75f) {
        vx = ts; vy = ns; vz = ss;
        u1 = (u1 - 0.5f) * 4.0f;
    } else {
        vx = ns; vy = ss; vz = ts;
        u1 = (u1 - 0.75f) * 4.0f;
    }

    int channel = static_cast<int>(u1 * 3.0f);
    channel = std::clamp(channel, 0, 2);
    u1 = u1 * 3.0f - static_cast<float>(channel);

    const float radius = bssrdf.Sample_Sr(channel, u2.x);
    if (radius < 0.0f) return out;
    const float phi = 2.0f * Pi * u2.y;

    const float max_radius = bssrdf.Sample_Sr(channel, 0.999f);
    if (radius >= max_radius) return out;
    const float length = 2.0f * std::sqrt(
        std::max(0.0f, max_radius * max_radius - radius * radius));

    const Vec3f disk = vx * (radius * std::cos(phi)) + vy * (radius * std::sin(phi));
    const Point3f base = po + disk - vz * (length * 0.5f);
    const Ray3f probe{base, vz};

    const BvhProbeHits hits = IntersectAccelerationProbe(
        scene.Geometry(),
        acceleration,
        probe,
        MeshPrimitiveHandle{target_primitive_index},
        InstanceHandle{target_instance_index},
        SphereHandle{target_sphere_index},
        1.0e-5f,
        length);
    if (hits.count == 0) return out;

    int selected = static_cast<int>(u1 * static_cast<float>(hits.count));
    selected = std::clamp(selected, 0, hits.count - 1);
    const BvhHit& chosen = hits.hits[selected];

    out.pi = probe.At(chosen.t);
    if (chosen.sphere.IsValid()) {
        const RenderSphere& sphere =
            scene.spheres[static_cast<std::size_t>(chosen.sphere.Value())];
        out.ni = SphereNormal(sphere.center, sphere.radius, out.pi);
        out.sphere_index = chosen.sphere.Value();
    } else {
        const TriangleRef triangle = LocateTriangle(scene.Geometry(), chosen.triangle_index);
        out.ni = GeometricNormal(scene.Geometry(), triangle);
        out.primitive_index = chosen.mesh_primitive.Value();
        out.triangle_index = chosen.triangle_index;
        out.bary_u = chosen.bary_u;
        out.bary_v = chosen.bary_v;
    }

    out.sp = bssrdf.Sp(Length(po - out.pi));
    out.pdf = bssrdf.Pdf_Sp(po, ss, ts, ns, out.pi, out.ni) /
        static_cast<float>(hits.count);
    out.hit = true;
    return out;
}

}  // namespace yr
