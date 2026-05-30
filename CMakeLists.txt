cmake_minimum_required(VERSION 3.24)

project(YaoRay
    VERSION 0.1.0
    DESCRIPTION "Physically based offline path tracer"
    LANGUAGES C CXX
)

option(YAORAY_ENABLE_CUDA "Build the CUDA backend when available" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(yaoray_core STATIC
    src/core/version.cpp
    src/core/diagnostic.cpp
    src/core/transform.cpp
)
target_include_directories(yaoray_core PUBLIC include)

add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/film_checkpoint.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

add_library(tinyexr STATIC
    external/tinyexr/deps/miniz/miniz.c
)
target_include_directories(tinyexr PUBLIC
    external/tinyexr
    external/tinyexr/deps/miniz
)

target_link_libraries(yaoray_film PUBLIC yaoray_core PRIVATE stb tinyexr)

add_library(yaoray_assets STATIC
    src/assets/ply_loader.cpp
)
target_include_directories(yaoray_assets PUBLIC include)
target_link_libraries(yaoray_assets PUBLIC yaoray_core)

add_library(yaoray_pbrt STATIC
    src/pbrt/pbrt_scene.cpp
)
target_include_directories(yaoray_pbrt PUBLIC include)
target_link_libraries(yaoray_pbrt PUBLIC yaoray_core)

add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/environment.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/render_scene.cpp
    src/render/scene_compiler.cpp
    src/render/render_scene_hash.cpp
    src/render/shading.cpp
    src/render/texture.cpp
    src/render/tensor_file.cpp
    src/render/measured_brdf.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_pbrt PRIVATE yaoray_assets stb tinyexr)

add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
    src/backends/cpu/cpu_material.cpp
    src/backends/cpu/cpu_prepared_scene.cpp
    src/backends/cpu/cpu_tile_scheduler.cpp
    src/backends/cpu/cpu_sampler.cpp
    src/backends/cpu/cpu_surface.cpp
    src/backends/cpu/cpu_path_tracer.cpp
    src/backends/cuda/cuda_backend.cpp
    src/backends/cuda/cuda_prepared_scene.cpp
)
target_include_directories(yaoray_backends PUBLIC include)
target_link_libraries(yaoray_backends PUBLIC yaoray_core yaoray_film yaoray_render)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/bvh_tests.cpp
    tests/sphere_tests.cpp
    tests/film_tests.cpp
    tests/render_scene_tests.cpp
    tests/pbrt_tests.cpp
    tests/bsdf_tests.cpp
    tests/environment_tests.cpp
    tests/mis_tests.cpp
    tests/light_sampling_tests.cpp
    tests/texture_tests.cpp
    tests/backend_tests.cpp
    tests/cpu_material_tests.cpp
    tests/cpu_surface_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
    tests/cpu_tile_scheduler_tests.cpp
    tests/cpu_sampler_tests.cpp
    tests/cpu_path_tracer_tests.cpp
    tests/analytic_light_tests.cpp
    tests/material_degradation_tests.cpp
    tests/scene_compiler_texture_tests.cpp
    tests/scene_compiler_environment_tests.cpp
    tests/scene_compiler_imagemap_degrade_tests.cpp
    tests/scene_compiler_imagemap_exr_tests.cpp
    tests/scene_compiler_film_iso_tests.cpp
    tests/scene_compiler_plymesh_degrade_tests.cpp
    tests/scene_compiler_envmap_degrade_tests.cpp
    tests/scene_compiler_scale_texture_tests.cpp
    tests/scene_compiler_wrap_mode_tests.cpp
    tests/scene_compiler_normal_map_tests.cpp
    tests/scene_compiler_tangent_tests.cpp
    tests/scene_compiler_coated_tests.cpp
    tests/scene_compiler_disk_tests.cpp
    tests/scene_compiler_loopsubdiv_tests.cpp
    tests/scene_compiler_spectrum_spd_tests.cpp
    tests/scene_compiler_interface_roughness_tests.cpp
    tests/exr_io_tests.cpp
    tests/rng_tests.cpp
    tests/layered_bsdf_tests.cpp
    tests/tensor_file_tests.cpp
    tests/measured_brdf_tests.cpp
    tests/scene_compiler_measured_tests.cpp
    tests/piecewise_linear_2d_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_pbrt yaoray_render yaoray_assets yaoray_backends)
target_compile_definitions(yaoray_tests PRIVATE YAORAY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_pbrt yaoray_render yaoray_backends)

if(BUILD_TESTING)
    function(add_yaoray_cli_render_test name)
        set(options EXPECT_FAILURE RUN_IMAGE_SANITY)
        set(oneValueArgs SCENE OUTPUT BACKEND)
        set(multiValueArgs EXPECT_REGEX EXPECT_FILE)
        cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        if(NOT ARG_SCENE)
            message(FATAL_ERROR "add_yaoray_cli_render_test requires SCENE")
        endif()

        set(test_args
            "-DYAORAY_EXE=$<TARGET_FILE:yaoray>"
            "-DSCENE_PATH=${ARG_SCENE}"
            "-DEXPECT_FAILURE=${ARG_EXPECT_FAILURE}"
        )
        if(ARG_OUTPUT)
            list(APPEND test_args "-DOUTPUT_PATH=${ARG_OUTPUT}")
        endif()
        if(ARG_BACKEND)
            list(APPEND test_args "-DBACKEND=${ARG_BACKEND}")
        endif()

        list(LENGTH ARG_EXPECT_REGEX regex_count)
        list(APPEND test_args "-DEXPECT_REGEX_COUNT=${regex_count}")
        if(regex_count GREATER 0)
            math(EXPR last_regex_index "${regex_count} - 1")
            foreach(index RANGE 0 ${last_regex_index})
                list(GET ARG_EXPECT_REGEX ${index} regex)
                list(APPEND test_args "-DEXPECT_REGEX_${index}=${regex}")
            endforeach()
        endif()

        list(LENGTH ARG_EXPECT_FILE expected_file_count)
        list(APPEND test_args "-DEXPECT_FILE_COUNT=${expected_file_count}")
        if(expected_file_count GREATER 0)
            math(EXPR last_expected_file_index "${expected_file_count} - 1")
            foreach(index RANGE 0 ${last_expected_file_index})
                list(GET ARG_EXPECT_FILE ${index} expected_file)
                list(APPEND test_args "-DEXPECT_FILE_${index}=${expected_file}")
            endforeach()
        endif()

        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND} ${test_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_render_test.cmake"
        )
    endfunction()

    add_test(NAME yaoray_tests COMMAND yaoray_tests)
    add_test(NAME yaoray_cli_help COMMAND yaoray --help)
    set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
    add_test(NAME yaoray_cli_version COMMAND yaoray --version)
    set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
    add_test(NAME yaoray_cli_render_help COMMAND yaoray render --help)
    set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "PBRT")
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_minimal
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pbrt/minimal_triangle.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pbrt/out/minimal_pbrt.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_cornell_box
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/out/cornell_box_pbrt_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [^0]"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_material_studio
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/material_studio/material_studio_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/material_studio/out/material_studio_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [^0]"
            "Compiled textures: [^0]"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_texture_test
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/texture_test/texture_test_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/texture_test/out/texture_test_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [1-9]"
            "Compiled textures: [1-9]"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_coated_showcase
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/coated_showcase/out/coated_showcase_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [^0]"
    )
endif()
