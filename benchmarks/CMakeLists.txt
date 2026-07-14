add_executable(yaoray_cpu_benchmark cpu_benchmark.cpp)
set_target_properties(yaoray_cpu_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/benchmarks)
target_compile_definitions(yaoray_cpu_benchmark PRIVATE
    YAORAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(yaoray_cpu_benchmark PRIVATE
    yaoray_frontend_pbrt
    yaoray_backend_cpu
)
