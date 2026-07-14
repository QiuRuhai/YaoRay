add_library(yaoray_integrators STATIC
    bssrdf_probe.cpp
    mis.cpp
    path_direct_lighting.cpp
    path_integrator.cpp
    path_integrator_common.cpp
    surface_query.cpp
)
target_include_directories(yaoray_integrators PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_integrators PUBLIC
    yaoray_core
    yaoray_scene
    yaoray_geometry
    yaoray_accel
    yaoray_shading
    yaoray_lighting
)
