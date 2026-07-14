add_library(yaoray_geometry STATIC intersection.cpp)
target_include_directories(yaoray_geometry PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_geometry PUBLIC yaoray_core yaoray_scene)
