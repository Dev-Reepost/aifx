# AIFX — AI-powered OpenFX plugins bridging ComfyUI to any OFX host.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.28)

# ----------------------------------------------------------------------------
# Version — single source of truth.
#
# The repo-root VERSION file is the ONLY place the release version is written.
# CMake stamps it into every plugin's Info.plist (CFBundleVersion) and into the
# .ofx binary itself (see AIFX_BUILD_MARKER), and tools/release-*.sh read the
# same file, so a shipped bundle can always be identified after the fact.
# Before this existed every bundle claimed "1.0.0" and an installed plugin was
# indistinguishable from any other build -- which is exactly how a pre-0.1.5
# bundle survived on a Flare/macOS workstation and kept segfaulting.
# ----------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AIFX_VERSION LIMIT_COUNT 1)
string(STRIP "${AIFX_VERSION}" AIFX_VERSION)
if(NOT AIFX_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION file must contain a semver string, got: '${AIFX_VERSION}'")
endif()

project(AIFX VERSION ${AIFX_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ----------------------------------------------------------------------------
# Portable RPATH.
#
# Deps link statically into each .ofx (build-plugin.sh forces -o *:shared=False;
# conanfile.py defaults to it), so nothing is actually loaded from an RPATH. But
# CMake otherwise auto-derives RPATH entries from the Conan link directories and
# bakes absolute cache paths (/root/.conan2/..., ~/.conan2/...) into the binary
# — non-portable, and rejected by verify_binary_portability(). Pin RPATH to the
# binary's own directory and nothing else:
#   - CMAKE_BUILD_WITH_INSTALL_RPATH ON   -> use the install RPATH for the
#     build-tree binary too (bundles are assembled from build/ via POST_BUILD
#     copies, never `cmake --install`), and *suppress* the auto link-path RPATH.
#   - CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF -> don't append Conan link dirs.
#   - CMAKE_INSTALL_RPATH = the self-relative loader token per platform.
# Should any dep ever end up shared, this keeps it resolvable from inside the
# bundle instead of from the build host's cache.
# ----------------------------------------------------------------------------
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

# ----------------------------------------------------------------------------
# OpenFX dependency (via FetchContent).
#
# AIFX consumes the OpenFX C API headers and the C++ Support library from
# AcademySoftwareFoundation/openfx. They are not vendored — CMake downloads
# them at configure time, pinned to a specific tag. To upgrade against a
# newer OpenFX release, bump GIT_TAG below.
# ----------------------------------------------------------------------------

include(FetchContent)

# Suppress OpenFX subcomponents we don't need. AIFX plugins only depend on the
# Support library; Examples and host-side build artifacts are unused.
set(BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(OFX_SUPPORTS_OPENCLRENDER OFF CACHE BOOL "" FORCE)
set(OFX_SUPPORTS_CUDARENDER   OFF CACHE BOOL "" FORCE)

# Pinned to upstream main (2025-08-21). Bump to a tagged release as soon as
# OpenFX cuts one that includes the modern Examples layout and Info.plist.in.
# Held in a variable so the exact commit is stamped into the generated
# aifx_abi.h and, from there, into every shipped .ofx binary.
set(AIFX_OPENFX_TAG "96ccd1aab317971da43c70c92b187295d478db0c")

FetchContent_Declare(
    openfx
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    GIT_TAG        ${AIFX_OPENFX_TAG}
)
FetchContent_MakeAvailable(openfx)

# Expose OpenFX source root so plugin CMakeLists.txt files can reach Support/
# headers and the shared Info.plist.in template.
set(OPENFX_SOURCE_DIR ${openfx_SOURCE_DIR} CACHE INTERNAL "OpenFX source tree (FetchContent)")

# ----------------------------------------------------------------------------
# CRITICAL — vtable-layout consistency with the OFX Support library.
#
# OFX_SUPPORTS_OPENGLRENDER gates extra *virtual* methods on OFX::ImageEffect
# (contextAttached/contextDetached and the OpenGL render-arg overloads), so it
# changes that class's vtable layout. Upstream OpenFX defaults it ON and applies
# it with add_definitions()/add_compile_definitions() *inside its own directory
# scope* — so the OfxSupport library is built WITH it, but the macro never
# reaches our plugin sources in plugins/ (a sibling directory). The result is an
# ODR violation: OfxSupport dispatches host actions through ImageEffect's LONGER
# vtable while our plugins were compiled against a SHORTER one. The host then
# calls an action through a vtable slot that, in the plugin's vtable, holds a
# different virtual (observed: a render action landed on BasePlugin::buildWorkflow
# at slot 0xa0) — an immediate SIGSEGV when Flame instances and renders a node.
#
# Mirror the EXACT setting OfxSupport was built with onto every target defined
# below (the plugins), so both sides agree on the layout. Keyed off the same
# cache variable OpenFX's CMake set, so it tracks however the option is resolved.
# ----------------------------------------------------------------------------
if(OFX_SUPPORTS_OPENGLRENDER)
    add_compile_definitions(OFX_SUPPORTS_OPENGLRENDER)
endif()
if(OFX_SUPPORTS_OPENCLRENDER)
    add_compile_definitions(OFX_SUPPORTS_OPENCLRENDER)
endif()
if(OFX_SUPPORTS_CUDARENDER)
    add_compile_definitions(OFX_SUPPORTS_CUDARENDER)
endif()

# Mirroring the definitions is necessary but not sufficient: nothing yet FAILS
# when a target slips out of scope again. Record the flags OfxSupport was built
# with in a generated header, which plugins/common/aifx_abi_check.h turns into a
# static_assert against what each translation unit actually sees. A future
# refactor that loses the definitions breaks the build instead of shipping a
# bundle that segfaults on instancing.
foreach(_flag OPENGLRENDER OPENCLRENDER CUDARENDER)
    if(OFX_SUPPORTS_${_flag})
        set(AIFX_OFX_EXPECT_${_flag} 1)
    else()
        set(AIFX_OFX_EXPECT_${_flag} 0)
    endif()
endforeach()

set(AIFX_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/common/aifx_abi.h.in"
    "${AIFX_GENERATED_INCLUDE_DIR}/aifx_abi.h"
    @ONLY
)

# ----------------------------------------------------------------------------
# ComfyUI plugin third-party dependencies (Conan-provided, CONFIG-only).
#
# CONFIG-only resolution. CMake's built-in FindOpenSSL.cmake probes Homebrew's
# keg on Apple Silicon and bakes /opt/homebrew/... paths into LC_LOAD_DYLIB,
# which makes bundles non-portable. Restricting to CONFIG mode forces
# find_package() to consume Conan's *Config.cmake from CMAKE_PREFIX_PATH.
# ----------------------------------------------------------------------------

find_package(OpenSSL        CONFIG REQUIRED)
find_package(nlohmann_json  CONFIG REQUIRED)
find_package(httplib        CONFIG REQUIRED)
find_package(ixwebsocket    CONFIG REQUIRED)
find_package(tinyexr        CONFIG REQUIRED)
find_package(miniz          CONFIG REQUIRED)
find_package(spdlog                REQUIRED)

# ----------------------------------------------------------------------------
# AIFX plugins.
# ----------------------------------------------------------------------------

# Sentinel the plugins/ tree expects; preserved from the openfx-fork build to
# keep the plugin CMakeLists.txt files unchanged conceptually.
set(BUILD_COMFYUI_PLUGINS ON CACHE BOOL "Build AIFX ComfyUI plugins" FORCE)

add_subdirectory(plugins)

message(STATUS "AIFX configured.")
message(STATUS "  OpenFX source : ${OPENFX_SOURCE_DIR}")
message(STATUS "  Install dir   : ${PLUGIN_INSTALLDIR}")
message(STATUS "  Version       : ${AIFX_VERSION}")
message(STATUS "  OFX ABI       : v1-gl${AIFX_OFX_EXPECT_OPENGLRENDER}-cl${AIFX_OFX_EXPECT_OPENCLRENDER}-cu${AIFX_OFX_EXPECT_CUDARENDER}")
