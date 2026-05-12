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
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)
target_link_libraries(yaoray_film PUBLIC yaoray_core)

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE external/tomlplusplus)

add_library(yaoray_scene STATIC
    src/scene/diagnostic.cpp
    src/scene/scene.cpp
    src/scene/scene_parser.cpp
)
target_include_directories(yaoray_scene PUBLIC include)
target_link_libraries(yaoray_scene PUBLIC yaoray_core PRIVATE tomlplusplus)

add_library(yaoray_render STATIC
    src/render/scene_compiler.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/render_scene_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render)

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
    add_test(NAME yaoray_cli_help COMMAND yaoray --help)
    set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
    add_test(NAME yaoray_cli_version COMMAND yaoray --version)
    set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
    add_test(NAME yaoray_cli_render_help COMMAND yaoray render --help)
    set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "yaoray render")
    add_test(NAME yaoray_cli_render_cpu COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml --backend cpu)
    set_tests_properties(yaoray_cli_render_cpu PROPERTIES PASS_REGULAR_EXPRESSION "Requested backend: cpu")
    add_test(NAME yaoray_cli_render_cuda COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml --backend cuda)
    set_tests_properties(yaoray_cli_render_cuda PROPERTIES PASS_REGULAR_EXPRESSION "Requested backend: cuda")
    add_test(NAME yaoray_cli_render_missing_file
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/nope.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'scene file not found') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_invalid_scene
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/invalid_width.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'render.width') { exit 1 }"
    )
endif()
