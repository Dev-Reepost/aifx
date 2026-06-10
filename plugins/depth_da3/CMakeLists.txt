# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause

# ComfyUI Depth Anything V3 Plugin

if(APPLE)
    add_library(DepthAnything3 MODULE
        depth_da3_plugin.cpp
    )
else()
    add_library(DepthAnything3 SHARED
        depth_da3_plugin.cpp
    )
endif()

set_target_properties(DepthAnything3 PROPERTIES
    SUFFIX ".ofx"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

target_include_directories(DepthAnything3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

target_link_libraries(DepthAnything3 PRIVATE
    ComfyUICommon
    OfxSupport
    nlohmann_json::nlohmann_json
    httplib::httplib
    ixwebsocket::ixwebsocket
    OpenSSL::SSL
    OpenSSL::Crypto
    tinyexr::tinyexr
    miniz::miniz
)

if(APPLE)
    target_link_libraries(DepthAnything3 PRIVATE "-framework CoreFoundation")
endif()

# macOS: create .ofx.bundle structure at build time
if(APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/DepthAnything3.ofx.bundle/Contents/MacOS")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/DepthAnything3.ofx.bundle/Contents/Resources")

    set(PLUGIN_NAME "DepthAnything3")
    set(PLUGIN_EXE "DepthAnything3.ofx")
    set(BUNDLE_IDENTIFIER "com.comfyui.DepthAnything3")
    set(BUNDLE_SIGNATURE "????")
    set(PLUGIN_VERSION "1.0.0")

    configure_file(${OPENFX_SOURCE_DIR}/Examples/Info.plist.in
                   "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/DepthAnything3.ofx.bundle/Contents/Info.plist")

    add_custom_command(TARGET DepthAnything3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DepthAnything3> "${BUNDLE_DIR}/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"

        COMMENT "Creating DepthAnything3.ofx.bundle structure with resources"
    )


aifx_merge_defaults(DepthAnything3 "${RESOURCES_DIR}")
endif()

# Linux: create .ofx.bundle structure at build time
if(UNIX AND NOT APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/DepthAnything3.ofx.bundle/Contents/Linux-x86-64")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/DepthAnything3.ofx.bundle/Contents/Resources")

    add_custom_command(TARGET DepthAnything3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DepthAnything3> "${BUNDLE_DIR}/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"

        COMMENT "Creating DepthAnything3.ofx.bundle structure with resources (Linux)"
    )

aifx_merge_defaults(DepthAnything3 "${RESOURCES_DIR}")
endif()

# Windows: create .ofx.bundle structure at build time. Mirrors the Linux
# block above with Win64 as the OS-specific binary directory.
# Assumes a single-config generator (Ninja/Makefile via the Conan preset);
# for Visual Studio multi-config builds, BUNDLE_DIR would need to be
# wrapped with $<CONFIG> to land in the right per-config output folder.
if(WIN32)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/DepthAnything3.ofx.bundle/Contents/Win64")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/DepthAnything3.ofx.bundle/Contents/Resources")

    add_custom_command(TARGET DepthAnything3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:DepthAnything3> "${BUNDLE_DIR}/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"

        COMMENT "Creating DepthAnything3.ofx.bundle structure with resources (Windows)"
    )

    aifx_merge_defaults(DepthAnything3 "${RESOURCES_DIR}")
endif()

message(STATUS "ComfyUI Depth Anything V3 plugin configured with bundled resources")