add_library(yaoray_runtime INTERFACE)
target_include_directories(yaoray_runtime INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_runtime INTERFACE yaoray_core yaoray_scene yaoray_film)

add_library(yaoray_backend_cuda_stub STATIC
    cuda_stub_backend.cpp
    cuda_stub_prepared_scene.cpp
)
target_include_directories(yaoray_backend_cuda_stub PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_backend_cuda_stub PUBLIC yaoray_runtime)
