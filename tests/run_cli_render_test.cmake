if(NOT DEFINED YAORAY_EXE)
    message(FATAL_ERROR "YAORAY_EXE is required")
endif()
if(NOT DEFINED SCENE_PATH)
    message(FATAL_ERROR "SCENE_PATH is required")
endif()

if(DEFINED OUTPUT_PATH AND NOT OUTPUT_PATH STREQUAL "")
    file(REMOVE "${OUTPUT_PATH}")
endif()

if(DEFINED EXPECT_FILE_COUNT AND EXPECT_FILE_COUNT GREATER 0)
    math(EXPR last_expected_file_index "${EXPECT_FILE_COUNT} - 1")
    foreach(index RANGE 0 ${last_expected_file_index})
        set(expected_file_var "EXPECT_FILE_${index}")
        if(DEFINED ${expected_file_var})
            file(REMOVE "${${expected_file_var}}")
        endif()
    endforeach()
endif()

set(command "${YAORAY_EXE}" render "${SCENE_PATH}")
if(DEFINED BACKEND AND NOT BACKEND STREQUAL "")
    list(APPEND command --backend "${BACKEND}")
endif()

execute_process(
    COMMAND ${command}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)

set(output "${stdout}${stderr}")
message("${output}")

if(EXPECT_FAILURE)
    if(result EQUAL 0)
        message(FATAL_ERROR "Expected render command to fail, but it exited with 0")
    endif()
else()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Render command failed with exit code ${result}")
    endif()
endif()

if(DEFINED EXPECT_REGEX_COUNT AND EXPECT_REGEX_COUNT GREATER 0)
    math(EXPR last_regex_index "${EXPECT_REGEX_COUNT} - 1")
    foreach(index RANGE 0 ${last_regex_index})
        set(regex_var "EXPECT_REGEX_${index}")
        if(NOT DEFINED ${regex_var})
            message(FATAL_ERROR "${regex_var} is required")
        endif()
        if(NOT output MATCHES "${${regex_var}}")
            message(FATAL_ERROR "Expected output to match regex: ${${regex_var}}")
        endif()
    endforeach()
endif()

if(DEFINED OUTPUT_PATH AND NOT OUTPUT_PATH STREQUAL "")
    if(NOT EXISTS "${OUTPUT_PATH}")
        message(FATAL_ERROR "Expected render output was not written: ${OUTPUT_PATH}")
    endif()
    file(READ "${OUTPUT_PATH}" png_signature LIMIT 8 HEX)
    string(TOLOWER "${png_signature}" png_signature)
    if(NOT png_signature STREQUAL "89504e470d0a1a0a")
        message(FATAL_ERROR "Render output does not have a PNG header: ${OUTPUT_PATH}")
    endif()
endif()

if(DEFINED EXPECT_FILE_COUNT AND EXPECT_FILE_COUNT GREATER 0)
    math(EXPR last_expected_file_index "${EXPECT_FILE_COUNT} - 1")
    foreach(index RANGE 0 ${last_expected_file_index})
        set(expected_file_var "EXPECT_FILE_${index}")
        if(NOT DEFINED ${expected_file_var})
            message(FATAL_ERROR "${expected_file_var} is required")
        endif()
        if(NOT EXISTS "${${expected_file_var}}")
            message(FATAL_ERROR "Expected generated file was not written: ${${expected_file_var}}")
        endif()
        file(SIZE "${${expected_file_var}}" expected_file_size)
        if(expected_file_size EQUAL 0)
            message(FATAL_ERROR "Expected generated file is empty: ${${expected_file_var}}")
        endif()
    endforeach()
endif()

if(DEFINED IMAGE_SANITY_EXE AND NOT IMAGE_SANITY_EXE STREQUAL "")
    if(NOT DEFINED OUTPUT_PATH OR OUTPUT_PATH STREQUAL "")
        message(FATAL_ERROR "OUTPUT_PATH is required when IMAGE_SANITY_EXE is set")
    endif()
    execute_process(
        COMMAND "${IMAGE_SANITY_EXE}" "${OUTPUT_PATH}"
        RESULT_VARIABLE image_sanity_result
        OUTPUT_VARIABLE image_sanity_stdout
        ERROR_VARIABLE image_sanity_stderr
    )
    message("${image_sanity_stdout}${image_sanity_stderr}")
    if(NOT image_sanity_result EQUAL 0)
        message(FATAL_ERROR "Image sanity check failed with exit code ${image_sanity_result}")
    endif()
endif()
