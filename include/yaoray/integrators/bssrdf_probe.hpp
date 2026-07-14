#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/shading/bssrdf.hpp>

namespace yr {

struct RenderSceneIR;
struct RenderAcceleration;

// Result of the integrator's scene-space probe for a subsurface exit point.
struct BssrdfProbeSample {
    bool hit = false;
    Point3f pi{0, 0, 0};
    Vec3f ni{0, 0, 1};
    int primitive_index = -1;
    int triangle_index = -1;
    int sphere_index = -1;
    float bary_u = 0.0f;
    float bary_v = 0.0f;
    Color3f sp{0, 0, 0};
    float pdf = 0.0f;
};

// Importance-sample an exit point by probing the entry object through the
// acceleration structure. The BSSRDF owns the radial distribution; this
// integrator helper owns scene traversal and crossing selection.
BssrdfProbeSample SampleBssrdfProbe(
    const TabulatedBSSRDF& bssrdf,
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
    int target_primitive_index, int target_instance_index, int target_sphere_index,
    float u1, const Vec2f& u2);

}  // namespace yr
