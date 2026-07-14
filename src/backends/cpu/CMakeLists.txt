add_library(yaoray_backend_cpu STATIC
    cpu_backend.cpp
    cpu_debug_renderer.cpp
    cpu_path_tracer.cpp
    cpu_prepared_scene.cpp
    cpu_tile_scheduler.cpp
    cpu_worker_pool.cpp
)
target_include_directories(yaoray_backend_cpu PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_backend_cpu PUBLIC
    yaoray_runtime
    yaoray_accel
    yaoray_sampling
    yaoray_integrators
)
