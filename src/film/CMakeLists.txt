add_library(yaoray_film STATIC
    denoiser.cpp
    film.cpp
    film_checkpoint.cpp
    image_writer.cpp
    tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(yaoray_film PUBLIC yaoray_core PRIVATE stb tinyexr)

if(YAORAY_ENABLE_OIDN)
    target_compile_definitions(yaoray_film PRIVATE YAORAY_HAS_OIDN=1)
    if(TARGET OpenImageDenoise)
        target_link_libraries(yaoray_film PRIVATE OpenImageDenoise)
    elseif(TARGET OpenImageDenoise::OpenImageDenoise)
        target_link_libraries(yaoray_film PRIVATE OpenImageDenoise::OpenImageDenoise)
    else()
        message(FATAL_ERROR "OpenImageDenoise package did not export a supported CMake target")
    endif()
endif()
