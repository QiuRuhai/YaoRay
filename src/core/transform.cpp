#include <yaoray/core/transform.hpp>

#include <cmath>

namespace yr {

namespace {
constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}
} // namespace

Mat4f Multiply(Mat4f a, Mat4f b) {
    Mat4f result;
    result.m.fill(0.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[static_cast<std::size_t>(column * 4 + row)] +=
                    a.m[static_cast<std::size_t>(k * 4 + row)] *
                    b.m[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Point3f TransformPoint(Mat4f transform, Point3f point) {
    return Point3f{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8] * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9] * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]
    };
}

Vec3f TransformVector(Mat4f transform, Vec3f value) {
    return Vec3f{
        transform.m[0] * value.x + transform.m[4] * value.y + transform.m[8] * value.z,
        transform.m[1] * value.x + transform.m[5] * value.y + transform.m[9] * value.z,
        transform.m[2] * value.x + transform.m[6] * value.y + transform.m[10] * value.z
    };
}

Vec3f TransformNormal(Mat4f transform, Vec3f normal) {
    const float a = transform.m[0];
    const float b = transform.m[4];
    const float c = transform.m[8];
    const float d = transform.m[1];
    const float e = transform.m[5];
    const float f = transform.m[9];
    const float g = transform.m[2];
    const float h = transform.m[6];
    const float i = transform.m[10];

    const float determinant =
        a * (e * i - f * h) -
        b * (d * i - f * g) +
        c * (d * h - e * g);
    if (std::fabs(determinant) <= 1.0e-12f) {
        const Vec3f fallback = Normalize(TransformVector(transform, normal));
        return LengthSquared(fallback) > 0.0f ? fallback : Normalize(normal);
    }

    const float inv_det = 1.0f / determinant;
    return Normalize(Vec3f{
        ((e * i - f * h) * normal.x + (f * g - d * i) * normal.y + (d * h - e * g) * normal.z) * inv_det,
        ((c * h - b * i) * normal.x + (a * i - c * g) * normal.y + (b * g - a * h) * normal.z) * inv_det,
        ((b * f - c * e) * normal.x + (c * d - a * f) * normal.y + (a * e - b * d) * normal.z) * inv_det
    });
}

Mat4f TranslationMatrix(Vec3f translation) {
    Mat4f result;
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

Mat4f ScaleMatrix(Vec3f scale) {
    Mat4f result;
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

Mat4f RotationAxisMatrix(float angle_degrees, Vec3f axis) {
    const Vec3f unit_axis = Normalize(axis);
    if (LengthSquared(unit_axis) == 0.0f) {
        return Mat4f{};
    }

    const float radians = DegreesToRadians(angle_degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0f - c;
    const float x = unit_axis.x;
    const float y = unit_axis.y;
    const float z = unit_axis.z;

    Mat4f result;
    result.m[0] = t * x * x + c;
    result.m[1] = t * x * y + s * z;
    result.m[2] = t * x * z - s * y;

    result.m[4] = t * x * y - s * z;
    result.m[5] = t * y * y + c;
    result.m[6] = t * y * z + s * x;

    result.m[8] = t * x * z + s * y;
    result.m[9] = t * y * z - s * x;
    result.m[10] = t * z * z + c;
    return result;
}

Mat4f Inverse(Mat4f m) {
    // Full 4x4 inverse via the adjugate formula. Returns identity on singular
    // input. Works for general affine transforms (including non-orthonormal
    // upper-left 3x3), not just rigid-body ones.
    const float* a = m.m.data();
    Mat4f result;
    float* o = result.m.data();

    o[0]  =  a[5]  * a[10] * a[15] - a[5]  * a[11] * a[14] - a[9]  * a[6]  * a[15] +
             a[9]  * a[7]  * a[14] + a[13] * a[6]  * a[11] - a[13] * a[7]  * a[10];
    o[4]  = -a[4]  * a[10] * a[15] + a[4]  * a[11] * a[14] + a[8]  * a[6]  * a[15] -
             a[8]  * a[7]  * a[14] - a[12] * a[6]  * a[11] + a[12] * a[7]  * a[10];
    o[8]  =  a[4]  * a[9]  * a[15] - a[4]  * a[11] * a[13] - a[8]  * a[5]  * a[15] +
             a[8]  * a[7]  * a[13] + a[12] * a[5]  * a[11] - a[12] * a[7]  * a[9];
    o[12] = -a[4]  * a[9]  * a[14] + a[4]  * a[10] * a[13] + a[8]  * a[5]  * a[14] -
             a[8]  * a[6]  * a[13] - a[12] * a[5]  * a[10] + a[12] * a[6]  * a[9];
    o[1]  = -a[1]  * a[10] * a[15] + a[1]  * a[11] * a[14] + a[9]  * a[2]  * a[15] -
             a[9]  * a[3]  * a[14] - a[13] * a[2]  * a[11] + a[13] * a[3]  * a[10];
    o[5]  =  a[0]  * a[10] * a[15] - a[0]  * a[11] * a[14] - a[8]  * a[2]  * a[15] +
             a[8]  * a[3]  * a[14] + a[12] * a[2]  * a[11] - a[12] * a[3]  * a[10];
    o[9]  = -a[0]  * a[9]  * a[15] + a[0]  * a[11] * a[13] + a[8]  * a[1]  * a[15] -
             a[8]  * a[3]  * a[13] - a[12] * a[1]  * a[11] + a[12] * a[3]  * a[9];
    o[13] =  a[0]  * a[9]  * a[14] - a[0]  * a[10] * a[13] - a[8]  * a[1]  * a[14] +
             a[8]  * a[2]  * a[13] + a[12] * a[1]  * a[10] - a[12] * a[2]  * a[9];
    o[2]  =  a[1]  * a[6]  * a[15] - a[1]  * a[7]  * a[14] - a[5]  * a[2]  * a[15] +
             a[5]  * a[3]  * a[14] + a[13] * a[2]  * a[7]  - a[13] * a[3]  * a[6];
    o[6]  = -a[0]  * a[6]  * a[15] + a[0]  * a[7]  * a[14] + a[4]  * a[2]  * a[15] -
             a[4]  * a[3]  * a[14] - a[12] * a[2]  * a[7]  + a[12] * a[3]  * a[6];
    o[10] =  a[0]  * a[5]  * a[15] - a[0]  * a[7]  * a[13] - a[4]  * a[1]  * a[15] +
             a[4]  * a[3]  * a[13] + a[12] * a[1]  * a[7]  - a[12] * a[3]  * a[5];
    o[14] = -a[0]  * a[5]  * a[14] + a[0]  * a[6]  * a[13] + a[4]  * a[1]  * a[14] -
             a[4]  * a[2]  * a[13] - a[12] * a[1]  * a[6]  + a[12] * a[2]  * a[5];
    o[3]  = -a[1]  * a[6]  * a[11] + a[1]  * a[7]  * a[10] + a[5]  * a[2]  * a[11] -
             a[5]  * a[3]  * a[10] - a[9]  * a[2]  * a[7]  + a[9]  * a[3]  * a[6];
    o[7]  =  a[0]  * a[6]  * a[11] - a[0]  * a[7]  * a[10] - a[4]  * a[2]  * a[11] +
             a[4]  * a[3]  * a[10] + a[8]  * a[2]  * a[7]  - a[8]  * a[3]  * a[6];
    o[11] = -a[0]  * a[5]  * a[11] + a[0]  * a[7]  * a[9]  + a[4]  * a[1]  * a[11] -
             a[4]  * a[3]  * a[9]  - a[8]  * a[1]  * a[7]  + a[8]  * a[3]  * a[5];
    o[15] =  a[0]  * a[5]  * a[10] - a[0]  * a[6]  * a[9]  - a[4]  * a[1]  * a[10] +
             a[4]  * a[2]  * a[9]  + a[8]  * a[1]  * a[6]  - a[8]  * a[2]  * a[5];

    const float det = a[0] * o[0] + a[1] * o[4] + a[2] * o[8] + a[3] * o[12];
    if (std::fabs(det) <= 1.0e-12f) {
        return Mat4f{};
    }
    const float inv_det = 1.0f / det;
    for (int i = 0; i < 16; ++i) {
        o[i] *= inv_det;
    }
    return result;
}

Mat4f LookAtMatrix(Point3f eye, Point3f target, Vec3f up) {
    Vec3f forward = Normalize(target - eye);
    if (LengthSquared(forward) == 0.0f) {
        return Mat4f{};
    }
    Vec3f right = Normalize(Cross(forward, Normalize(up)));
    if (LengthSquared(right) == 0.0f) {
        return Mat4f{};
    }
    Vec3f true_up = Cross(right, forward);

    // First build worldFromCamera (C2W) with the eye in the translation
    // column, mirroring PBRT v4's `LookAt()` helper.
    Mat4f world_from_camera;
    world_from_camera.m[0] = right.x;
    world_from_camera.m[1] = right.y;
    world_from_camera.m[2] = right.z;

    world_from_camera.m[4] = true_up.x;
    world_from_camera.m[5] = true_up.y;
    world_from_camera.m[6] = true_up.z;

    world_from_camera.m[8] = forward.x;
    world_from_camera.m[9] = forward.y;
    world_from_camera.m[10] = forward.z;

    world_from_camera.m[12] = eye.x;
    world_from_camera.m[13] = eye.y;
    world_from_camera.m[14] = eye.z;

    // PBRT v4's `LookAt()` returns a Transform whose forward matrix is the
    // INVERSE of worldFromCamera — i.e. world-to-camera. The CTM stored
    // after a LookAt or Transform directive is therefore W2C. This
    // convention is what the dining-room (and other Tungsten-converted)
    // scenes expect.
    return Inverse(world_from_camera);
}

} // namespace yr
