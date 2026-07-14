add_library(yaoray_io STATIC
    image_loader_common.cpp
    image_loader_exr.cpp
    image_loader_hdr.cpp
    image_loader_ldr.cpp
    ply_loader.cpp
    tensor_file.cpp
)
target_include_directories(yaoray_io PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_io PUBLIC yaoray_core yaoray_scene PRIVATE stb tinyexr)
