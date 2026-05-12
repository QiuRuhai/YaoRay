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

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
endif()
