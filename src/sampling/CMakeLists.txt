add_library(yaoray_sampling STATIC sampler.cpp)
target_include_directories(yaoray_sampling PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_sampling PUBLIC yaoray_core yaoray_scene)
