#pragma once

#include <array>
#include <yaoray/core/vec.hpp>

namespace yr {

struct Mat4f {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

Mat4f Multiply(Mat4f a, Mat4f b);
Point3f TransformPoint(Mat4f transform, Point3f point);
Vec3f TransformVector(Mat4f transform, Vec3f value);
Vec3f TransformNormal(Mat4f transform, Vec3f normal);

Mat4f TranslationMatrix(Vec3f translation);
Mat4f ScaleMatrix(Vec3f scale);
Mat4f RotationAxisMatrix(float angle_degrees, Vec3f axis);
Mat4f LookAtMatrix(Point3f eye, Point3f target, Vec3f up);

} // namespace yr
