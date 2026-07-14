add_executable(yaoray main.cpp)
set_target_properties(yaoray PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(yaoray PRIVATE
    yaoray_frontend_pbrt
    yaoray_backend_factory
)
