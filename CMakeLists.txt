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
    src/film/film_checkpoint.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

target_link_libraries(yaoray_film PUBLIC yaoray_core PRIVATE stb)

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE external/tomlplusplus)

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE external/tinyobjloader)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE external/tinygltf external/stb)

add_library(yaoray_scene STATIC
    src/scene/diagnostic.cpp
    src/scene/scene.cpp
    src/scene/scene_parser.cpp
    src/scene/scene_world.cpp
)
target_include_directories(yaoray_scene PUBLIC include)
target_link_libraries(yaoray_scene PUBLIC yaoray_core PRIVATE tomlplusplus)

add_library(yaoray_assets STATIC
    src/assets/gltf_loader.cpp
    src/assets/obj_loader.cpp
    src/assets/ply_loader.cpp
)
target_include_directories(yaoray_assets PUBLIC include)
target_link_libraries(yaoray_assets PUBLIC yaoray_core yaoray_scene PRIVATE tinygltf tinyobjloader)
target_compile_options(yaoray_assets PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

add_library(yaoray_pbrt STATIC
    src/pbrt/pbrt_scene.cpp
)
target_include_directories(yaoray_pbrt PUBLIC include)
target_link_libraries(yaoray_pbrt PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets)

add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/environment.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
    src/render/render_scene_hash.cpp
    src/render/shading.cpp
    src/render/texture.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets stb)

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
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/scene_world_tests.cpp
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
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_assets yaoray_pbrt yaoray_backends)
target_compile_definitions(yaoray_tests PRIVATE YAORAY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backends)

if(BUILD_TESTING)
    add_executable(yaoray_glass_showcase_visual_sanity
        tests/glass_showcase_visual_sanity.cpp
    )
    target_link_libraries(yaoray_glass_showcase_visual_sanity PRIVATE stb)

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

        if(ARG_RUN_IMAGE_SANITY)
            list(APPEND test_args "-DIMAGE_SANITY_EXE=$<TARGET_FILE:yaoray_glass_showcase_visual_sanity>")
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
    set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "integrator")
    add_yaoray_cli_render_test(yaoray_cli_render_cpu
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: debug_direct"
            "Rendered image:"
            "Rays traced:"
            "Shadow rays:"
            "Occluded shadow rays:"
            "BVH nodes:"
            "BVH max depth:"
            "BVH node tests:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_obj
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: debug_direct"
            "Compiled triangles: 2"
            "BVH nodes:"
            "BVH max depth:"
            "BVH node tests:"
            "Shadow rays:"
            "Occluded shadow rays:"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_textured_obj
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/textured_quad.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/textured_quad.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Compiled triangles: 2"
            "Compiled materials:"
            "Compiled textures:"
            "Texture memory MiB:"
            "Prepare seconds:"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_gltf
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/gltf_textured_asset.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/gltf_textured_asset.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Compiled triangles: 2"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_cornell_box
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/cornell_box.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/cornell_box.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: debug_direct"
            "Compiled triangles: 38"
            "Rendered image:"
            "Shadow rays:"
            "BVH nodes:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_path
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/path_tracer_bounce.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/path_tracer_bounce.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Compiled materials:"
            "Compiled textures:"
            "Texture memory MiB:"
            "Prepare seconds:"
            "Rendered image:"
            "Threads: 2"
            "Rays traced:"
            "Samples/sec:"
            "Rays/sec:"
            "Shadow rays:"
            "BVH nodes:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_hdri_showcase
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/hdri_lighting_showcase.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/hdri_lighting_showcase.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Shadow rays:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_glass_showcase
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/glass_showcase.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/glass_showcase.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Shadow rays:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_glass_showcase_visual_sanity
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/glass_showcase.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/glass_showcase.png"
        BACKEND cpu
        RUN_IMAGE_SANITY
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_offline_checkpoint
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_checkpoint.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.png"
        BACKEND cpu
        EXPECT_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.preview.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.yrcheckpoint"
        EXPECT_REGEX
            "Progress:"
            "Checkpoint image:"
            "Checkpoint state:"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_offline_resume
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_resume.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_resume.png"
        BACKEND cpu
        EXPECT_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_resume.preview.png"
        EXPECT_REGEX
            "Resumed checkpoint:"
            "Rendered image:"
    )
    set_tests_properties(yaoray_cli_render_offline_resume PROPERTIES DEPENDS yaoray_cli_render_offline_checkpoint)
    add_yaoray_cli_render_test(yaoray_cli_render_offline_unsupported_cuda
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_checkpoint.toml"
        BACKEND cuda
        EXPECT_FAILURE
        EXPECT_REGEX "offline workflow supports only cpu path renders"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_cuda
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml"
        BACKEND cuda
        EXPECT_FAILURE
        EXPECT_REGEX "Render backend preparation failed: CUDA backend preparation is not implemented yet"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_unsupported_asset
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/unsupported_asset.toml"
        EXPECT_FAILURE
        EXPECT_REGEX "asset import not implemented yet"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_missing_file
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/nope.toml"
        EXPECT_FAILURE
        EXPECT_REGEX "scene file not found"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_invalid_scene
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/invalid_width.toml"
        EXPECT_FAILURE
        EXPECT_REGEX "render.width"
    )
endif()
