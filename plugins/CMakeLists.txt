# AIFX — ComfyUI bridge OFX plugins.
# SPDX-License-Identifier: BSD-3-Clause
#
# Builds a shared static library `ComfyUICommon` with the HTTP client, EXR I/O,
# job queue, and OFX base class that every AIFX plugin inherits from, then
# adds each plugin as a subdirectory. Each plugin produces a single .ofx
# shared library / module, packaged into a .ofx.bundle by per-plugin build
# rules.

if(NOT BUILD_COMFYUI_PLUGINS)
    message(STATUS "AIFX plugins disabled. Use -DBUILD_COMFYUI_PLUGINS=ON to enable.")
    return()
endif()

message(STATUS "Building AIFX plugins")

# --- Build-time defaults.json merge ---------------------------------------
#
# Each plugin's bundle ships a Contents/Resources/config/defaults.json that
# seeds the OFX parameter panel on first instantiation (ComfyUI server URL,
# mount paths, controls, project info). The server + controls blocks are
# studio-wide and live in a single source of truth at config/defaults-base.json
# at the repo root; each plugin owns only its project-specific block (plus
# any per-plugin override, e.g. a shorter controls.timeout) in its own
# defaults-project.json. The merge happens at build time so the bundle ends
# up with a single, complete defaults.json — no run-time file lookups.
#
# aifx_merge_defaults(TARGET BUNDLE_RESOURCES_DIR) writes the merged file to
# ${BUNDLE_RESOURCES_DIR}/config/defaults.json as a POST_BUILD step.
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(aifx_merge_defaults TARGET BUNDLE_RESOURCES_DIR)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_RESOURCES_DIR}/config"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/merge-defaults.py"
            "${CMAKE_SOURCE_DIR}/config/defaults-base.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/defaults-project.json"
            "${BUNDLE_RESOURCES_DIR}/config/defaults.json"
        COMMENT "Merging defaults.json into ${TARGET} bundle"
        VERBATIM
    )
endfunction()

# --- Shared infrastructure -------------------------------------------------

add_library(ComfyUICommon STATIC
    common/comfyui_client.cpp
    common/comfyui_base_plugin.cpp
    common/comfyui_image_io.cpp
    common/async_job_manager.cpp
)

# Position-independent code so the static lib can link into .ofx shared libs.
set_target_properties(ComfyUICommon PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(ComfyUICommon PUBLIC
    OfxSupport
    nlohmann_json::nlohmann_json
    httplib::httplib
    ixwebsocket::ixwebsocket
    tinyexr::tinyexr
    miniz::miniz
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog_header_only
)

if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(ComfyUICommon PUBLIC ${COREFOUNDATION_FRAMEWORK})
endif()

target_include_directories(ComfyUICommon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${OPENFX_SOURCE_DIR}/Support/include
)

# --- Plugins ---------------------------------------------------------------

# V1 release scope (7 plugins).
add_subdirectory(depth_da3)
add_subdirectory(depth_crafter)
add_subdirectory(normal_crafter)
add_subdirectory(segmentation_sam3)
add_subdirectory(matte_mama)
add_subdirectory(matte_ma2)
add_subdirectory(upscale_seedvr2)

# Tests build only if the dir was copied.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

message(STATUS "AIFX plugins configured: 7 plugins targeting OpenFX 1.4+.")
