add_library(yaoray_accel STATIC
    acceleration.cpp
    bvh_builder.cpp
    bvh_builder_common.cpp
    bvh_split_policy.cpp
    bvh_serial_builder.cpp
    bvh_parallel_builder.cpp
    bvh_traversal.cpp
    two_level_bvh.cpp
)
target_include_directories(yaoray_accel PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_accel PUBLIC yaoray_core yaoray_scene yaoray_geometry)
