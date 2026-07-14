add_library(yaoray_lighting STATIC
    analytic_light_sampling.cpp
    environment.cpp
    light_sampling.cpp
    scene_view.cpp
)
target_include_directories(yaoray_lighting PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_lighting PUBLIC yaoray_core yaoray_scene yaoray_shading)
