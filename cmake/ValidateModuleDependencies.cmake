function(yaoray_validate_public_module module allowed_modules)
    file(GLOB_RECURSE module_headers CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/yaoray/${module}/*.hpp")
    foreach(header IN LISTS module_headers)
        file(STRINGS "${header}" yaoray_includes
            REGEX "^#include <yaoray/[^/]+/")
        foreach(include_line IN LISTS yaoray_includes)
            string(REGEX REPLACE
                "^#include <yaoray/([^/]+)/.*$" "\\1"
                dependency "${include_line}")
            if(NOT dependency IN_LIST allowed_modules)
                file(RELATIVE_PATH relative_header "${PROJECT_SOURCE_DIR}" "${header}")
                message(FATAL_ERROR
                    "Forbidden public dependency: ${relative_header} includes "
                    "module '${dependency}'. Allowed modules for '${module}': "
                    "${allowed_modules}")
            endif()
        endforeach()
    endforeach()
endfunction()

yaoray_validate_public_module(core "core")
yaoray_validate_public_module(scene "core;scene")
yaoray_validate_public_module(io "core;scene;io")
yaoray_validate_public_module(geometry "core;scene;geometry")
yaoray_validate_public_module(accel "core;scene;geometry;accel")
yaoray_validate_public_module(sampling "core;scene;sampling")
yaoray_validate_public_module(shading "core;scene;geometry;io;shading")
yaoray_validate_public_module(lighting "core;scene;geometry;shading;lighting")
