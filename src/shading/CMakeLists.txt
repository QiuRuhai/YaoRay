add_library(yaoray_shading STATIC
    bsdf.cpp
    bsdf_common.cpp
    bsdf_layered.cpp
    bsdf_measured.cpp
    bsdf_microfacet.cpp
    bssrdf.cpp
    catmull_rom.cpp
    material_evaluator.cpp
    measured_brdf.cpp
    scene_view.cpp
    texture.cpp
)
target_include_directories(yaoray_shading PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_shading PUBLIC
    yaoray_core
    yaoray_scene
    yaoray_geometry
    yaoray_io
)
