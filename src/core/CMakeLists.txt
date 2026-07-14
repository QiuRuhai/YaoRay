add_library(yaoray_core STATIC
    transform.cpp
    version.cpp
)
target_include_directories(yaoray_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
