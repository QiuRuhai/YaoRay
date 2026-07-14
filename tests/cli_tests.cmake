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
            -P "${PROJECT_SOURCE_DIR}/tests/run_cli_render_test.cmake"
    )
    set_tests_properties(${name} PROPERTIES LABELS "correctness;integration")
endfunction()

add_test(NAME yaoray_cli_help COMMAND yaoray --help)
set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
set_tests_properties(yaoray_cli_help PROPERTIES LABELS "debug;smoke")
add_test(NAME yaoray_cli_version COMMAND yaoray --version)
set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
set_tests_properties(yaoray_cli_version PROPERTIES LABELS "debug;smoke")
add_test(NAME yaoray_cli_render_help COMMAND yaoray render --help)
set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "PBRT")
set_tests_properties(yaoray_cli_render_help PROPERTIES LABELS "debug;smoke")

add_yaoray_cli_render_test(yaoray_cli_render_pbrt_minimal
    SCENE "${PROJECT_SOURCE_DIR}/tests/fixtures/pbrt/minimal_triangle.pbrt"
    OUTPUT "${PROJECT_SOURCE_DIR}/tests/fixtures/pbrt/out/minimal_pbrt.png"
    BACKEND cpu
    EXPECT_REGEX "Integrator: path" "Rendered image:"
)
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_cornell_box
    SCENE "${PROJECT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt_smoke.pbrt"
    OUTPUT "${PROJECT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/out/cornell_box_pbrt_smoke.png"
    BACKEND cpu
    EXPECT_REGEX "Integrator: path" "Rendered image:" "Hits: [^0]"
)
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_material_studio
    SCENE "${PROJECT_SOURCE_DIR}/scenes/pbrt/material_studio/material_studio_smoke.pbrt"
    OUTPUT "${PROJECT_SOURCE_DIR}/scenes/pbrt/material_studio/out/material_studio_smoke.png"
    BACKEND cpu
    EXPECT_REGEX
        "Integrator: path"
        "Rendered image:"
        "Hits: [^0]"
        "Compiled textures: [^0]"
)
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_texture_test
    SCENE "${PROJECT_SOURCE_DIR}/scenes/pbrt/texture_test/texture_test_smoke.pbrt"
    OUTPUT "${PROJECT_SOURCE_DIR}/scenes/pbrt/texture_test/out/texture_test_smoke.png"
    BACKEND cpu
    EXPECT_REGEX
        "Integrator: path"
        "Rendered image:"
        "Hits: [1-9]"
        "Compiled textures: [1-9]"
)
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_coated_showcase
    SCENE "${PROJECT_SOURCE_DIR}/scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt"
    OUTPUT "${PROJECT_SOURCE_DIR}/scenes/pbrt/coated_showcase/out/coated_showcase_smoke.png"
    BACKEND cpu
    EXPECT_REGEX "Integrator: path" "Rendered image:" "Hits: [^0]"
)
