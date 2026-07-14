add_library(yaoray_frontend_pbrt STATIC
    pbrt_scene.cpp
    pbrt_tokenizer.cpp
    scene_compiler.cpp
    scene_compiler_common.cpp
    scene_compiler_settings.cpp
    scene_compiler_textures.cpp
    scene_compiler_materials.cpp
    scene_compiler_conductor_materials.cpp
    scene_compiler_normal_maps.cpp
    scene_compiler_special_materials.cpp
    scene_compiler_texture_params.cpp
    scene_compiler_analytic_geometry.cpp
    scene_compiler_geometry.cpp
    scene_compiler_mesh_geometry.cpp
    scene_compiler_ply_geometry.cpp
    scene_compiler_lights.cpp
)
target_include_directories(yaoray_frontend_pbrt PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_frontend_pbrt PUBLIC
    yaoray_core
    yaoray_scene
    yaoray_io
    yaoray_shading
    yaoray_lighting
)
