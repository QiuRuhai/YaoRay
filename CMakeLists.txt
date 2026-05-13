cmake_minimum_required(VERSION 3.24)

project(YaoRay
    VERSION 0.1.0
    DESCRIPTION "Physically based offline path tracer"
    LANGUAGES CXX
)

option(YAORAY_ENABLE_CUDA "Build the CUDA backend when available" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(yaoray_core STATIC
    src/core/version.cpp
)
target_include_directories(yaoray_core PUBLIC include)

add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)
target_link_libraries(yaoray_film PUBLIC yaoray_core)

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE external/tomlplusplus)

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE external/tinyobjloader)

add_library(yaoray_scene STATIC
    src/scene/diagnostic.cpp
    src/scene/scene.cpp
    src/scene/scene_parser.cpp
)
target_include_directories(yaoray_scene PUBLIC include)
target_link_libraries(yaoray_scene PUBLIC yaoray_core PRIVATE tomlplusplus)

add_library(yaoray_assets STATIC
    src/assets/obj_loader.cpp
)
target_include_directories(yaoray_assets PUBLIC include)
target_link_libraries(yaoray_assets PUBLIC yaoray_core PRIVATE tinyobjloader)
target_compile_options(yaoray_assets PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/scene_compiler.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets)

add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
)
target_include_directories(yaoray_backends PUBLIC include)
target_link_libraries(yaoray_backends PUBLIC yaoray_core yaoray_film yaoray_render)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/render_scene_tests.cpp
    tests/backend_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_assets yaoray_backends)
target_compile_definitions(yaoray_tests PRIVATE YAORAY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backends)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
    add_test(NAME yaoray_cli_help COMMAND yaoray --help)
    set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
    add_test(NAME yaoray_cli_version COMMAND yaoray --version)
    set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
    add_test(NAME yaoray_cli_render_help COMMAND yaoray render --help)
    set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "yaoray render")
    add_test(NAME yaoray_cli_render_cpu
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_obj
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_cuda
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cuda 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'CUDA backend not implemented yet') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_unsupported_asset
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'asset import not implemented yet') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_missing_file
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/nope.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'scene file not found') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_invalid_scene
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/invalid_width.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'render.width') { exit 1 }"
    )
endif()
