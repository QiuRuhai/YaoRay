add_library(yaoray_scene STATIC
    camera.cpp
    diagnostic.cpp
    render_scene.cpp
    render_scene_hash.cpp
)
target_include_directories(yaoray_scene PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_scene PUBLIC yaoray_core)
