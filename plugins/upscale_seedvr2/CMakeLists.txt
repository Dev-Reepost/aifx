# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause

# ComfyUI UpscaleSeedVR2 Plugin

if(APPLE)
    add_library(UpscaleSeedVR2 MODULE
        upscale_seedvr2_plugin.cpp
    )
else()
    add_library(UpscaleSeedVR2 SHARED
        upscale_seedvr2_plugin.cpp
    )
endif()

set_target_properties(UpscaleSeedVR2 PROPERTIES
    SUFFIX ".ofx"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

target_include_directories(UpscaleSeedVR2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

target_link_libraries(UpscaleSeedVR2 PRIVATE
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
    target_link_libraries(UpscaleSeedVR2 PRIVATE "-framework CoreFoundation")
endif()

if(APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UpscaleSeedVR2.ofx.bundle/Contents/MacOS")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UpscaleSeedVR2.ofx.bundle/Contents/Resources")

    set(PLUGIN_NAME "UpscaleSeedVR2")
    set(PLUGIN_EXE "UpscaleSeedVR2.ofx")
    set(BUNDLE_IDENTIFIER "com.comfyui.UpscaleSeedVR2")
    set(BUNDLE_SIGNATURE "????")
    set(PLUGIN_VERSION "1.0.0")

    configure_file(${OPENFX_SOURCE_DIR}/Examples/Info.plist.in
                   "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UpscaleSeedVR2.ofx.bundle/Contents/Info.plist")

    add_custom_command(TARGET UpscaleSeedVR2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:UpscaleSeedVR2> "${BUNDLE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"
        COMMENT "Creating UpscaleSeedVR2.ofx.bundle structure with resources"
    )


aifx_merge_defaults(UpscaleSeedVR2 "${RESOURCES_DIR}")
endif()

if(UNIX AND NOT APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/UpscaleSeedVR2.ofx.bundle/Contents/Linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/UpscaleSeedVR2.ofx.bundle/Resources")

    add_custom_command(TARGET UpscaleSeedVR2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:UpscaleSeedVR2> "${BUNDLE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"
        COMMENT "Creating UpscaleSeedVR2.ofx.bundle structure with resources (Linux)"
    )

aifx_merge_defaults(UpscaleSeedVR2 "${RESOURCES_DIR}")
endif()

# Windows: create .ofx.bundle structure at build time. Mirrors the Linux
# block above with Win64-${ARCH} as the OS-specific binary directory.
# Assumes a single-config generator (Ninja/Makefile via the Conan preset);
# for Visual Studio multi-config builds, BUNDLE_DIR would need to be
# wrapped with $<CONFIG> to land in the right per-config output folder.
if(WIN32)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/UpscaleSeedVR2.ofx.bundle/Contents/Win64-${CMAKE_SYSTEM_PROCESSOR}")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/UpscaleSeedVR2.ofx.bundle/Resources")

    add_custom_command(TARGET UpscaleSeedVR2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:UpscaleSeedVR2> "${BUNDLE_DIR}/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"

        COMMENT "Creating UpscaleSeedVR2.ofx.bundle structure with resources (Windows)"
    )

    aifx_merge_defaults(UpscaleSeedVR2 "${RESOURCES_DIR}")
endif()

message(STATUS "ComfyUI UpscaleSeedVR2 plugin configured with bundled resources")