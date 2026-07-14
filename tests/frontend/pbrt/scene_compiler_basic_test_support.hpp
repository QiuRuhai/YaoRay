#pragma once

#include <utility>
#include <vector>

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>

namespace yr::test_support {

inline PbrtScene MakeBasicPbrtScene() {
    PbrtScene scene;
    scene.source_path = "test.pbrt";
    scene.source_root = ".";
    scene.film.type = "rgb";
    scene.film.params.push_back(PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    scene.film.params.push_back(PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    scene.camera.type = "perspective";
    scene.camera.params.push_back(PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    scene.camera_transform = Mat4f{};
    scene.integrator.type = "path";
    scene.sampler.type = "independent";
    return scene;
}

inline void AddBasicSphere(PbrtScene& scene) {
    PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = Mat4f{};
    scene.shapes.push_back(std::move(shape));
}

} // namespace yr::test_support
