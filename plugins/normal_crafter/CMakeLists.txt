# Copyright OpenFX and contributors to the OpenFX project.
# SPDX-License-Identifier: BSD-3-Clause

# ComfyUI NormalCrafter Plugin

if(APPLE)
    add_library(NormalCrafter MODULE
        normal_crafter_plugin.cpp
    )
else()
    add_library(NormalCrafter SHARED
        normal_crafter_plugin.cpp
    )
endif()

set_target_properties(NormalCrafter PROPERTIES
    SUFFIX ".ofx"
    PREFIX ""
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
)

target_include_directories(NormalCrafter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${OPENFX_SOURCE_DIR}/Support/include
)

target_link_libraries(NormalCrafter PRIVATE
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
    target_link_libraries(NormalCrafter PRIVATE "-framework CoreFoundation")
endif()

if(APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/NormalCrafter.ofx.bundle/Contents/MacOS")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/NormalCrafter.ofx.bundle/Contents/Resources")

    set(PLUGIN_NAME "NormalCrafter")
    set(PLUGIN_EXE "NormalCrafter.ofx")
    set(BUNDLE_IDENTIFIER "com.comfyui.NormalCrafter")
    set(BUNDLE_SIGNATURE "????")
    set(PLUGIN_VERSION "1.0.0")

    configure_file(${OPENFX_SOURCE_DIR}/Examples/Info.plist.in
                   "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/NormalCrafter.ofx.bundle/Contents/Info.plist")

    add_custom_command(TARGET NormalCrafter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:NormalCrafter> "${BUNDLE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"
        COMMENT "Creating NormalCrafter.ofx.bundle structure with resources"
    )


aifx_merge_defaults(NormalCrafter "${RESOURCES_DIR}")
endif()

if(UNIX AND NOT APPLE)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/NormalCrafter.ofx.bundle/Contents/Linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/NormalCrafter.ofx.bundle/Resources")

    add_custom_command(TARGET NormalCrafter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:NormalCrafter> "${BUNDLE_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"
        COMMENT "Creating NormalCrafter.ofx.bundle structure with resources (Linux)"
    )

aifx_merge_defaults(NormalCrafter "${RESOURCES_DIR}")
endif()

# Windows: create .ofx.bundle structure at build time. Mirrors the Linux
# block above with Win64-${ARCH} as the OS-specific binary directory.
# Assumes a single-config generator (Ninja/Makefile via the Conan preset);
# for Visual Studio multi-config builds, BUNDLE_DIR would need to be
# wrapped with $<CONFIG> to land in the right per-config output folder.
if(WIN32)
    set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/NormalCrafter.ofx.bundle/Contents/Win64-${CMAKE_SYSTEM_PROCESSOR}")
    set(RESOURCES_DIR "${CMAKE_BINARY_DIR}/NormalCrafter.ofx.bundle/Resources")

    add_custom_command(TARGET NormalCrafter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:NormalCrafter> "${BUNDLE_DIR}/"

        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "${RESOURCES_DIR}"

        COMMENT "Creating NormalCrafter.ofx.bundle structure with resources (Windows)"
    )

    aifx_merge_defaults(NormalCrafter "${RESOURCES_DIR}")
endif()

message(STATUS "ComfyUI NormalCrafter plugin configured with bundled resources")