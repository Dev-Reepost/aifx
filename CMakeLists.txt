# AIFX — AI-powered OpenFX plugins bridging ComfyUI to any OFX host.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.28)
project(AIFX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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

FetchContent_Declare(
    openfx
    GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
    # Pinned to upstream main (2025-08-21). Bump to a tagged release as soon as
    # OpenFX cuts one that includes the modern Examples layout and Info.plist.in.
    GIT_TAG        96ccd1aab317971da43c70c92b187295d478db0c
)
FetchContent_MakeAvailable(openfx)

# Expose OpenFX source root so plugin CMakeLists.txt files can reach Support/
# headers and the shared Info.plist.in template.
set(OPENFX_SOURCE_DIR ${openfx_SOURCE_DIR} CACHE INTERNAL "OpenFX source tree (FetchContent)")

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
