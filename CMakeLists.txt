cmake_minimum_required(VERSION 3.24)

project(YaoRay
    VERSION 0.1.0
    DESCRIPTION "Physically based offline path tracer"
    LANGUAGES C CXX
)

option(YAORAY_ENABLE_CUDA "Build the CUDA backend when available" OFF)
option(YAORAY_BUILD_PERFORMANCE_TESTS "Build opt-in CPU performance benchmarks" OFF)
option(YAORAY_ENABLE_OIDN "Enable optional Intel Open Image Denoise integration" OFF)

if(YAORAY_ENABLE_OIDN)
    find_package(OpenImageDenoise CONFIG REQUIRED)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/ValidateModuleDependencies.cmake)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

add_library(tinyexr STATIC external/tinyexr/deps/miniz/miniz.c)
target_include_directories(tinyexr PUBLIC
    external/tinyexr
    external/tinyexr/deps/miniz
)

add_subdirectory(src/core)
add_subdirectory(src/scene)
add_subdirectory(src/sampling)
add_subdirectory(src/film)
add_subdirectory(src/io)
add_subdirectory(src/geometry)
add_subdirectory(src/accel)
add_subdirectory(src/shading)
add_subdirectory(src/lighting)
add_subdirectory(src/integrators)
add_subdirectory(src/frontend/pbrt)
add_subdirectory(src/runtime)
add_subdirectory(src/backends/cpu)

if(YAORAY_BUILD_PERFORMANCE_TESTS)
    add_subdirectory(benchmarks)
endif()

add_library(yaoray_backend_factory STATIC src/runtime/backend.cpp)
target_include_directories(yaoray_backend_factory PUBLIC include)
target_link_libraries(yaoray_backend_factory PUBLIC
    yaoray_runtime
    yaoray_backend_cpu
    yaoray_backend_cuda_stub
)

add_subdirectory(src/app)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
