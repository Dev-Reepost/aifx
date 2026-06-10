#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.

# OpenFX Plugin Build and Install Script - Cross-Platform Version
#
# Builds any OpenFX plugin from any directory structure
# Supports Linux, macOS, and Windows (Git Bash/MSYS2)

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Detect platform and set plugin paths
detect_platform() {
    case "$OSTYPE" in
        linux-gnu*) PLATFORM="linux" ;;
        darwin*)    PLATFORM="macos" ;;
        msys*|cygwin*|mingw*) PLATFORM="windows" ;;
        *)
            log_error "Unsupported platform: $OSTYPE"
            exit 1
            ;;
    esac
}

# Get platform-specific plugin paths
get_plugin_paths() {
    detect_platform
    case "$PLATFORM" in
        macos)
            SYSTEM_PLUGIN_DIR="/Library/OFX/Plugins"
            USER_INSTALL_DIR="$HOME/Library/OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
        linux)
            SYSTEM_PLUGIN_DIR="/usr/OFX/Plugins"
            USER_INSTALL_DIR="$HOME/.OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
        windows)
            SYSTEM_PLUGIN_DIR="/c/Program Files/Common Files/OFX/Plugins"
            USER_INSTALL_DIR="$HOME/AppData/Roaming/OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
    esac

}

# Function to show usage
show_usage() {
    echo "Usage: $0 <plugin-directory> [target-name] [options]"
    echo
    echo "Arguments:"
    echo "  plugin-directory    Path to plugin directory (relative to AIFX root)"
    echo "  target-name         CMake target name (defaults to directory name)"
    echo
    echo "Options:"
    echo "  -d, --debug              Build in debug mode"
    echo "  -c, --clean              Clean build before building"
    echo "  -i, --install            Install to user OFX library (~/Library/OFX/Plugins on macOS)"
    echo "  --install-dir DIR        Install to custom directory (implies -i)"
    echo "  -v, --verbose            Verbose build output"
    echo "  --bundle-name NAME       Custom bundle name (defaults to target name)"
    echo "  -h, --help               Show this help message"
    echo
    echo "Examples:"
    echo "  $0 plugins/depth_da3 DepthAnythingV3        # Build only"
    echo "  $0 plugins/depth_da3 DepthAnythingV3 -i     # Build and install"
    echo "  $0 Examples/Invert -d -v                                     # Debug build"
    echo "  $0 MyPlugin MyTarget --install-dir /Library/OFX/Plugins      # System install"
}

# Default values
PLUGIN_DIR=""
TARGET_NAME=""
BUNDLE_NAME=""
BUILD_TYPE="Release"
CLEAN_BUILD=false
DO_INSTALL=false
INSTALL_DIR=""
VERBOSE=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
# Initialize platform-specific path variables (SYSTEM_PLUGIN_DIR, USER_INSTALL_DIR, DEV_PLUGIN_DIR)
get_plugin_paths

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -i|--install)
            DO_INSTALL=true
            shift
            ;;
        -v|--verbose)
            VERBOSE="--verbose"
            shift
            ;;
        --bundle-name)
            BUNDLE_NAME="$2"
            shift 2
            ;;
        --install-dir)
            INSTALL_DIR="$2"
            DO_INSTALL=true
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        -*)
            log_error "Unknown option $1"
            show_usage
            exit 1
            ;;
        *)
            if [[ -z "$PLUGIN_DIR" ]]; then
                PLUGIN_DIR="$1"
            elif [[ -z "$TARGET_NAME" ]]; then
                TARGET_NAME="$1"
            else
                log_error "Too many arguments"
                show_usage
                exit 1
            fi
            shift
            ;;
    esac
done

# Check if plugin directory is provided
if [[ -z "$PLUGIN_DIR" ]]; then
    log_error "Plugin directory is required"
    show_usage
    exit 1
fi

# Set default target name if not provided
if [[ -z "$TARGET_NAME" ]]; then
    TARGET_NAME=$(basename "$PLUGIN_DIR")
fi

# Set default bundle name if not provided
if [[ -z "$BUNDLE_NAME" ]]; then
    BUNDLE_NAME="$TARGET_NAME"
fi

# Check if we're in the AIFX root directory
check_openfx_root() {
    if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]] || [[ ! -d "$REPO_ROOT/plugins" ]]; then
        log_error "Not in AIFX root directory or REPO_ROOT not set correctly"
        log_info "Current directory: $(pwd)"
        log_info "REPO_ROOT: $REPO_ROOT"
        exit 1
    fi
}

# Check if plugin directory exists
check_plugin_exists() {
    local full_path="$REPO_ROOT/$PLUGIN_DIR"
    
    if [[ ! -d "$full_path" ]]; then
        log_error "Plugin directory not found: $full_path"
        log_info "Make sure the path is relative to the AIFX root directory"
        exit 1
    fi
    
    # Check if it has a CMakeLists.txt
    if [[ ! -f "$full_path/CMakeLists.txt" ]]; then
        log_warning "No CMakeLists.txt found in $full_path"
        log_info "Plugin may not be buildable independently"
    fi
    
    log_info "Found plugin directory: $full_path"
}

# Clean build directory
clean_build() {
    if [[ "$CLEAN_BUILD" == true ]]; then
        log_info "Cleaning build directory..."
        if [[ -d "$REPO_ROOT/build" ]]; then
            rm -rf "$REPO_ROOT/build/$BUILD_TYPE"
            log_success "Build directory cleaned"
        fi
    fi
}

# Configure CMake
configure_cmake() {
    log_info "Configuring CMake ($BUILD_TYPE build)..."

    cd "$REPO_ROOT"

    # Ensure Conan is available
    if ! command -v conan &> /dev/null; then
        # Try to find Conan in common locations
        for path in ~/.local/bin ~/Library/Python/*/bin ~/.pyenv/versions/*/bin; do
            if [[ -f "$path/conan" ]]; then
                export PATH="$PATH:$path"
                break
            fi
        done
    fi

    if ! command -v conan &> /dev/null; then
        log_error "Conan not found in PATH"
        exit 1
    fi

    # Configure with appropriate build type
    local build_type_arg=""
    if [[ "$BUILD_TYPE" == "Debug" ]]; then
        build_type_arg="Debug"
    else
        build_type_arg="Release"
    fi

    # Detect if this is a ComfyUI plugin
    local is_comfyui_plugin=false
    local conan_extra_args=()
    if [[ "$PLUGIN_DIR" == "plugins/"* ]]; then
        is_comfyui_plugin=true
        conan_extra_args=()
        log_info "Detected ComfyUI plugin - enabling ComfyUI dependencies"
    fi

    # Force static linkage of all third-party deps, overriding whatever the
    # developer's `default` profile says. CLI `-o` has the highest precedence in
    # Conan 2 (above profile [options] and the consumer's default_options), so a
    # profile carrying `*:shared=True` can't slip shared libs — and their
    # absolute Conan-cache RUNPATHs — into the .ofx. expat is pinned back to
    # shared to match conanfile.py (it's OpenFX HostSupport's, never in the
    # bundle). Result: a self-contained, portable .ofx; see
    # verify_binary_portability().
    local conan_opts=(-o "*:shared=False" -o "expat/*:shared=True")

    # Install Conan dependencies and generate build files if needed
    if [[ ! -f "build/$BUILD_TYPE/generators/CMakePresets.json" ]] || [[ "$is_comfyui_plugin" == true ]]; then
        log_info "Installing Conan dependencies..."
        if ! conan install -s build_type=$BUILD_TYPE -pr:b=default "${conan_opts[@]}" "${conan_extra_args[@]}" --build=missing .; then
            log_warning "Conan install with --build=missing failed, retrying with --build='*' (builds all from source)..."
            conan install -s build_type=$BUILD_TYPE -pr:b=default "${conan_opts[@]}" "${conan_extra_args[@]}" --build="*" .
        fi
    fi

    # Mark the start of this build. find_plugin_binary uses this marker's mtime
    # as a freshness gate: any .ofx older than this is from a previous build
    # (possibly with a different target name) and must be ignored. Without
    # this gate, a target rename leaves a stale binary in the plugin dir that
    # the script happily picks up and "installs", with hours of confused
    # debugging when its load paths don't match today's source tree.
    BUILD_START_MARKER="$REPO_ROOT/build/$BUILD_TYPE/.build-start"
    mkdir -p "$(dirname "$BUILD_START_MARKER")"
    : > "$BUILD_START_MARKER"

    # Additional CMake flags. Use an array so values containing spaces stay
    # in a single argument without needing embedded quotes (which CMake would
    # otherwise see as part of the value).
    local cmake_flags=(-DBUILD_EXAMPLE_PLUGINS=TRUE)
    if [[ "$is_comfyui_plugin" == true ]]; then
        cmake_flags+=(-DBUILD_COMFYUI_PLUGINS=ON)
    fi

    # Always set PLUGIN_INSTALLDIR. cmake/OpenFX.cmake's add_ofx_plugin() writes
    # Info.plist directly into ${PLUGIN_INSTALLDIR}/<Target>.ofx.bundle/Contents
    # at *configure* time (via configure_file), so this path MUST be writable
    # by the current user. Its built-in default is /Library/OFX/Plugins/...
    # which is system-owned and typically read-only without sudo. Defaulting to
    # the user's OFX library guarantees the configure step succeeds; the
    # install_plugin step below still copies the final bundle to the requested
    # destination if --install-dir was used.
    local effective_installdir="${INSTALL_DIR:-$USER_INSTALL_DIR}"
    mkdir -p "$effective_installdir"
    cmake_flags+=("-DPLUGIN_INSTALLDIR=$effective_installdir")

    # Always configure with `cmake --fresh`. CMake caches resolved find_package
    # paths across runs, so a previous configure that happened to resolve (say)
    # OpenSSL against Homebrew leaves /opt/homebrew/... pinned in CMakeCache.txt
    # even after CMakeLists.txt is changed to use CONFIG-only mode. The cached
    # value silently wins on the next incremental configure, producing a binary
    # that loads only on the build machine. Pattern-matching the cache to detect
    # this is fragile — different packages cache under different variable names,
    # and new package managers (Nix, pkgsrc) slip through any denylist. The
    # reliable answer is to always start from scratch: configure on this project
    # is ~5–10 seconds with Conan's generators warmed, and object files survive
    # so incremental compilation is unaffected. Cheaper than any class of
    # "wrong library got picked" debugging.
    cmake --preset "conan-$(echo "${build_type_arg}" | tr '[:upper:]' '[:lower:]')" --fresh "${cmake_flags[@]}"
    log_success "CMake configuration completed"
}

# Build the plugin
build_plugin() {
    log_info "Building plugin target: $TARGET_NAME"
    
    cd "$REPO_ROOT"
    
    # Try to build the specific target first
    if cmake --build "build/$BUILD_TYPE" --config "$BUILD_TYPE" --target "$TARGET_NAME" --parallel $VERBOSE; then
        log_success "Plugin target built successfully"
    else
        # If specific target fails, try building all targets in the plugin directory
        log_info "Direct target build failed, trying to build all targets..."
        if cmake --build "build/$BUILD_TYPE" --config "$BUILD_TYPE" --parallel $VERBOSE; then
            log_success "Build completed (all targets)"
        else
            log_error "Plugin build failed"
            exit 1
        fi
    fi
}

# Find the plugin binary that *this* build produced. Critical: a previous
# build with a different target name may have left a stale .ofx in the same
# plugin dir. Without a target-existence gate this function happily picks the
# stale one, the portability check runs on it, and the resulting "not portable"
# error has nothing to do with today's build — confusing everyone.
#
# Gate: CMake creates `CMakeFiles/<TargetName>.dir/` for every target during
# configure. The directory's mtime updates on every reconfigure, even when no
# relink happens. Comparing against $BUILD_START_MARKER tells us whether the
# user-requested TARGET_NAME corresponds to a real target in THIS build.
#
# Once we've confirmed the target exists, the .ofx file's own mtime is
# irrelevant — CMake may have legitimately skipped the link step because
# nothing changed since the last build of the same target.
find_plugin_binary() {
    local search_base="build/$BUILD_TYPE"

    if [[ -z "${BUILD_START_MARKER:-}" || ! -f "$BUILD_START_MARKER" ]]; then
        log_error "Build start marker missing — configure_cmake must run first." >&2
        exit 1
    fi

    # 1. Validate the requested target was configured by *this* build.
    local target_subdir="$REPO_ROOT/$search_base/$PLUGIN_DIR/CMakeFiles/$TARGET_NAME.dir"
    if [[ ! -d "$target_subdir" || ! "$target_subdir" -nt "$BUILD_START_MARKER" ]]; then
        log_error "CMake target '$TARGET_NAME' was not configured by this build." >&2

        # List the targets that *were* configured under PLUGIN_DIR, so the
        # user can spot a rename or typo. Use the same freshness gate.
        local fresh_targets
        fresh_targets=$(find "$REPO_ROOT/$search_base/$PLUGIN_DIR/CMakeFiles" \
            -maxdepth 1 -type d -name "*.dir" -newer "$BUILD_START_MARKER" 2>/dev/null \
            | sed -E 's|.*/||; s|\.dir$||' \
            | grep -v '^$' || true)

        if [[ -n "$fresh_targets" ]]; then
            log_error "" >&2
            log_error "Targets actually configured under $PLUGIN_DIR in this build:" >&2
            echo "$fresh_targets" | sed 's|^|        |' >&2
            log_error "" >&2
            log_error "Was the CMake target renamed? Re-invoke build-plugin.sh with the name" >&2
            log_error "from 'add_library(<TargetName> ...)' in $PLUGIN_DIR/CMakeLists.txt." >&2
        else
            log_error "" >&2
            log_error "No targets at all were configured under $PLUGIN_DIR in this build." >&2
            log_error "Either CMake skipped the directory (check add_subdirectory in parent" >&2
            log_error "CMakeLists.txt) or the build failed before reaching it." >&2
        fi
        exit 1
    fi

    # 2. Target is real. Locate its .ofx output. Convention in this repo is
    #    <plugin_dir>/<target>.ofx, but allow a few fallbacks for plugins
    #    that set OUTPUT_NAME or build bundle structures themselves.
    local candidate_paths=(
        "$REPO_ROOT/$search_base/$PLUGIN_DIR/$TARGET_NAME.ofx"
        "$REPO_ROOT/$search_base/$PLUGIN_DIR/$TARGET_NAME.ofx/Contents/MacOS/$TARGET_NAME"
        "$REPO_ROOT/$search_base/Examples/$(basename "$PLUGIN_DIR")/$TARGET_NAME.ofx"
        "$REPO_ROOT/$search_base/Support/Plugins/$(basename "$PLUGIN_DIR")/$TARGET_NAME.ofx"
    )
    local plugin_binary=""
    for candidate in "${candidate_paths[@]}"; do
        if [[ -f "$candidate" ]]; then
            plugin_binary="$candidate"
            break
        fi
    done

    if [[ -z "$plugin_binary" ]]; then
        log_error "Target '$TARGET_NAME' was configured but no .ofx binary was found." >&2
        log_error "Looked for:" >&2
        for p in "${candidate_paths[@]}"; do
            log_error "  $p" >&2
        done
        log_error "The build may have failed silently for this target. Re-run with -v." >&2
        exit 1
    fi

    log_info "Found plugin binary: $plugin_binary" >&2
    echo "$plugin_binary"
}

# Locate the cmake-generated bundle or construct one from the raw binary.
# Outputs the bundle path to stdout; all other output goes to stderr.
locate_or_create_bundle() {
    local plugin_binary="$1"
    local bundle_name="${BUNDLE_NAME}.ofx.bundle"
    local binary_name
    binary_name=$(basename "$plugin_binary")

    # Prefer the cmake-generated bundle — it already has Info.plist (macOS-only,
    # generated by cmake) and Resources correctly in place.
    local cmake_bundle
    cmake_bundle=$(find "$REPO_ROOT/build/$BUILD_TYPE" -name "${TARGET_NAME}.ofx.bundle" -type d 2>/dev/null | head -1)

    if [[ -n "$cmake_bundle" && -d "$cmake_bundle/Contents" ]]; then
        log_info "Using cmake-generated bundle: $cmake_bundle" >&2
        { file "$(find "$cmake_bundle" -name "*.ofx" -type f | head -1)"; } >&2
        echo "$cmake_bundle"
        return
    fi

    # Fallback: construct bundle manually from the raw binary
    log_warning "No cmake bundle found, constructing bundle from binary" >&2
    local bundle_path="$REPO_ROOT/build/$BUILD_TYPE/$bundle_name"

    case "$PLATFORM" in
        macos)
            mkdir -p "$bundle_path/Contents/MacOS"
            cp "$plugin_binary" "$bundle_path/Contents/MacOS/"
            chmod +x "$bundle_path/Contents/MacOS/$binary_name"
            ;;
        linux|windows)
            mkdir -p "$bundle_path/Contents/Linux-x86-64"
            cp "$plugin_binary" "$bundle_path/Contents/Linux-x86-64/"
            chmod +x "$bundle_path/Contents/Linux-x86-64/$binary_name"
            ;;
    esac

    log_success "Bundle created: $bundle_path" >&2
    { file "$(find "$bundle_path" -name "*.ofx" -type f | head -1)"; } >&2
    echo "$bundle_path"
}

# Verify the built plugin binary will load on a clean target machine — not
# just the developer box that built it. Each platform has a different failure
# mode, but the symptom is the same: OFX hosts silently report the plugin as
# "not supported on this platform" and skip it, leaving the developer to guess
# why their bundle doesn't appear.
#
# Allowlist (not denylist): we explicitly enumerate what's portable and reject
# everything else. A denylist of known package-manager prefixes leaks the next
# time someone installs Nix, pkgsrc, Linuxbrew, or a custom /opt prefix.
#
# We fail hard rather than warn — silently shipping a non-portable bundle is
# exactly the regression this check exists to prevent.
verify_binary_portability() {
    local bundle_path="$1"
    case "$PLATFORM" in
        macos)   _verify_portability_macos   "$bundle_path" ;;
        linux)   _verify_portability_linux   "$bundle_path" ;;
        windows) _verify_portability_windows "$bundle_path" ;;
        *)       log_warning "No portability check for platform: $PLATFORM" ;;
    esac
}

# macOS: LC_LOAD_DYLIB entries record absolute paths at link time. Anything
# outside the system framework dirs or @-token-relative references makes the
# bundle load only on the build machine. Probe with `otool -L`.
_verify_portability_macos() {
    local bundle_path="$1"

    if ! command -v otool &>/dev/null; then
        log_warning "otool not found; skipping macOS portability check."
        return
    fi

    local plugin_binary
    plugin_binary=$(find "$bundle_path/Contents/MacOS" -maxdepth 1 -type f -name "*.ofx" 2>/dev/null | head -1)
    if [[ -z "$plugin_binary" ]]; then
        log_warning "No .ofx binary found in $bundle_path/Contents/MacOS; skipping portability check."
        return
    fi

    log_info "Verifying binary portability (macOS): $plugin_binary"

    # Allowlist of portable load-path prefixes:
    # - /System/Library/ and /usr/lib/ are present on every macOS install.
    # - @rpath / @loader_path / @executable_path are resolved at load time
    #   relative to the binary's own location, so they ship with the bundle.
    local allowed='^(/System/Library/|/usr/lib/|@rpath|@loader_path|@executable_path)'

    local bad_refs
    bad_refs=$(otool -L "$plugin_binary" 2>/dev/null \
        | tail -n +2 \
        | awk '{print $1}' \
        | grep -vE "$allowed" || true)

    if [[ -n "$bad_refs" ]]; then
        log_error "Binary has non-portable load paths in LC_LOAD_DYLIB:"
        echo "$bad_refs" | sed 's/^/        /'
        log_error ""
        log_error "Allowed prefixes: /System/Library/, /usr/lib/, @rpath, @loader_path, @executable_path"
        log_error "Anything else (Homebrew, MacPorts, Nix, build-host home dirs) means the bundle"
        log_error "loads only on the build machine; hosts will silently skip it as 'unsupported'."
        log_error ""
        log_error "Fix: make sure CMake resolved each dependency through Conan's *Config.cmake."
        log_error "build-plugin.sh now always configures with --fresh so cache poisoning can't"
        log_error "carry over a stale Homebrew resolution; verify the top-level CMakeLists uses"
        log_error "'find_package(... CONFIG REQUIRED)' for every Conan-supplied dep."
        exit 1
    fi

    # LC_LOAD_DYLIB install names are only half the story: a shared dependency
    # resolved as @rpath/lib... passes the check above but still needs an
    # LC_RPATH to be found at load time. If CMake baked that LC_RPATH as an
    # absolute Conan-cache path, the bundle loads only on this machine — and
    # otool -L never shows it. Inspect the load commands directly. (For each
    # LC_RPATH command otool -l prints the cmdsize line then a `path <p>` line.)
    local bad_rpaths
    bad_rpaths=$(otool -l "$plugin_binary" 2>/dev/null \
        | awk '/^[[:space:]]*cmd LC_RPATH$/{getline; getline; print $2}' \
        | grep -vE '^(@loader_path|@executable_path|@rpath)(/|$)' || true)

    if [[ -n "$bad_rpaths" ]]; then
        log_error "Binary has non-portable LC_RPATH entries:"
        echo "$bad_rpaths" | sed 's/^/        /'
        log_error ""
        log_error "Allowed: @loader_path / @executable_path / @rpath-relative only."
        log_error "An absolute LC_RPATH (Conan cache, Homebrew, build-host home) means a shared"
        log_error "dependency resolves only on the build machine. Prefer static linkage for"
        log_error "Conan-supplied deps (build-plugin.sh forces -o '*:shared=False')."
        exit 1
    fi

    log_success "Binary portability check passed (macOS)"
}

# Linux: PE-style absolute paths don't appear in ELF NEEDED entries — those
# are bare sonames (libc.so.6) resolved by the runtime linker. The real
# portability hazard is DT_RPATH / DT_RUNPATH pointing at build-host paths
# (/home/user/..., /opt/conan-cache/...). Allowlist: empty rpath or pure
# $ORIGIN-relative. Probe with `readelf -d`.
_verify_portability_linux() {
    local bundle_path="$1"

    if ! command -v readelf &>/dev/null; then
        log_warning "readelf not found; skipping Linux portability check."
        return
    fi

    local plugin_binary
    plugin_binary=$(find "$bundle_path" -type f -name "*.ofx" 2>/dev/null | head -1)
    if [[ -z "$plugin_binary" ]]; then
        log_warning "No .ofx binary found under $bundle_path; skipping portability check."
        return
    fi

    log_info "Verifying binary portability (Linux): $plugin_binary"

    # Pull RPATH/RUNPATH entries (one or both may be set). readelf prints them
    # as `(RPATH) Library rpath: [/path/one:/path/two]` — extract the bracketed
    # colon-separated list.
    local rpaths
    rpaths=$(readelf -d "$plugin_binary" 2>/dev/null \
        | grep -E '\((RPATH|RUNPATH)\)' \
        | sed -E 's/.*\[(.*)\].*/\1/' || true)

    if [[ -n "$rpaths" ]]; then
        # Allowlist: each colon-separated entry must be $ORIGIN or $ORIGIN/...
        local bad_rpath
        bad_rpath=$(echo "$rpaths" | tr ':' '\n' | grep -vE '^\$ORIGIN(/|$)' | grep -v '^$' || true)
        if [[ -n "$bad_rpath" ]]; then
            log_error "Binary has non-portable RPATH/RUNPATH entries:"
            echo "$bad_rpath" | sed 's/^/        /'
            log_error ""
            log_error "Allowed: empty, or \$ORIGIN-relative (e.g., \$ORIGIN, \$ORIGIN/../lib)."
            log_error "Absolute paths to /home, /opt, /usr/local, Conan cache dirs, etc. make the"
            log_error "bundle load only on the build machine."
            log_error ""
            log_error "Fix: set CMAKE_INSTALL_RPATH='\$ORIGIN' (and CMAKE_BUILD_WITH_INSTALL_RPATH=ON)"
            log_error "in CMakeLists.txt, or prefer static linkage for Conan-supplied deps."
            exit 1
        fi
    fi

    # Also reject any NEEDED entry that is an absolute path (rare but possible
    # if a library was linked by full path rather than -l<name>).
    local bad_needed
    bad_needed=$(readelf -d "$plugin_binary" 2>/dev/null \
        | grep -E '\(NEEDED\)' \
        | sed -E 's/.*\[(.*)\].*/\1/' \
        | grep '^/' || true)
    if [[ -n "$bad_needed" ]]; then
        log_error "Binary has NEEDED entries with absolute paths (not bare sonames):"
        echo "$bad_needed" | sed 's/^/        /'
        exit 1
    fi

    log_success "Binary portability check passed (Linux)"
}

# Windows: PE files don't embed absolute paths to dependencies — imports are
# bare DLL names resolved by the loader's search order. The hazard is
# depending on compiler-runtime DLLs (MinGW/MSYS2's libgcc_s, libstdc++,
# libwinpthread) that aren't on a stock Windows install. Either link those
# statically or bundle the DLLs alongside the .ofx. Probe with `objdump -p`
# (MSYS2/MinGW) or `dumpbin /dependents` (MSVC); skip the check if neither
# is available, since requiring a specific toolchain just to validate would
# break developers on the other toolchain.
_verify_portability_windows() {
    local bundle_path="$1"

    local plugin_binary
    plugin_binary=$(find "$bundle_path" -type f -name "*.ofx" 2>/dev/null | head -1)
    if [[ -z "$plugin_binary" ]]; then
        log_warning "No .ofx binary found under $bundle_path; skipping portability check."
        return
    fi

    local tool=""
    local imports=""
    if command -v dumpbin &>/dev/null; then
        tool="dumpbin"
        imports=$(dumpbin /dependents "$plugin_binary" 2>/dev/null \
            | awk 'tolower($1) ~ /\.dll$/ {print $1}')
    elif command -v objdump &>/dev/null; then
        tool="objdump"
        imports=$(objdump -p "$plugin_binary" 2>/dev/null \
            | grep -E '^[[:space:]]*DLL Name:' \
            | awk '{print $3}')
    else
        log_warning "Neither dumpbin nor objdump found; skipping Windows portability check."
        return
    fi

    log_info "Verifying binary portability (Windows, via $tool): $plugin_binary"

    # Reject DLLs that the MSYS2/MinGW toolchain emits as dynamic runtime
    # dependencies; these are typically absent on a clean Windows install.
    # If you genuinely need one of these, ship it inside the bundle's
    # Win64-x86_64/ directory and remove its prefix from this list.
    local forbidden_dlls
    forbidden_dlls=$(echo "$imports" \
        | grep -iE '^(libgcc|libstdc\+\+|libwinpthread|libssp|libgomp|libquadmath)' || true)

    if [[ -n "$forbidden_dlls" ]]; then
        log_error "Binary depends on compiler-runtime DLLs not present on stock Windows:"
        echo "$forbidden_dlls" | sed 's/^/        /'
        log_error ""
        log_error "These ship only with MSYS2/MinGW. On a target machine without that toolchain"
        log_error "the host will fail to load the plugin and report it as 'unsupported'."
        log_error ""
        log_error "Fix (preferred): link statically — add -static-libgcc -static-libstdc++ to"
        log_error "the target's link flags, or use the static MSVCRT runtime. Alternative: copy"
        log_error "the required DLLs into the bundle's Win64-x86_64/ directory."
        exit 1
    fi

    log_success "Binary portability check passed (Windows)"
}

# Install the bundle to the target directory
install_plugin() {
    local bundle_path="$1"

    if [[ "$DO_INSTALL" != true ]]; then
        log_info "Bundle ready (not installed). Use -i to install to $USER_INSTALL_DIR"
        return
    fi

    # If --install-dir was not set, default to the user OFX library
    local target_dir="${INSTALL_DIR:-$USER_INSTALL_DIR}"
    local bundle_name
    bundle_name=$(basename "$bundle_path")

    log_info "Installing to $target_dir ..."
    mkdir -p "$target_dir"
    rm -rf "$target_dir/$bundle_name"
    cp -r "$bundle_path" "$target_dir/"
    log_success "Plugin installed: $target_dir/$bundle_name"
}

# Verify installation
verify_installation() {
    if [[ "$DO_INSTALL" != true ]]; then
        return
    fi

    local target_dir="${INSTALL_DIR:-$USER_INSTALL_DIR}"
    local bundle_name="${BUNDLE_NAME}.ofx.bundle"

    log_info "Verifying installation..."
    if [[ -d "$target_dir/$bundle_name" ]]; then
        log_success "Plugin found at: $target_dir/$bundle_name"
    else
        log_warning "Plugin not found at expected location: $target_dir/$bundle_name"
    fi
}

# Print summary
print_summary() {
    local bundle_path="$1"
    echo
    log_success "Plugin build completed!"
    echo
    echo "Summary:"
    echo "  Plugin Directory: $PLUGIN_DIR"
    echo "  Target Name:      $TARGET_NAME"
    echo "  Bundle:           $bundle_path"
    echo "  Build Type:       $BUILD_TYPE"
    if [[ "$DO_INSTALL" == true ]]; then
        local target_dir="${INSTALL_DIR:-$USER_INSTALL_DIR}"
        echo "  Installed to:     $target_dir/${BUNDLE_NAME}.ofx.bundle"
    else
        echo
        echo "To install for host apps (Flame, Nuke, etc.):"
        echo "  $0 $PLUGIN_DIR $TARGET_NAME -i"
    fi
}

# Main execution
main() {
    echo "OpenFX Plugin Builder - Cross-Platform Version"
    echo "=========================================="
    echo "Plugin Directory: $PLUGIN_DIR"
    echo "Target Name: $TARGET_NAME"
    echo "Bundle Name: $BUNDLE_NAME"
    echo "Build Type: $BUILD_TYPE"
    if [[ "$CLEAN_BUILD" == true ]]; then echo "Clean Build: Yes"; fi
    if [[ "$DO_INSTALL" == true ]]; then echo "Install to:   ${INSTALL_DIR:-$USER_INSTALL_DIR}"; fi
    echo

    check_openfx_root
    check_plugin_exists
    clean_build
    configure_cmake
    build_plugin

    plugin_binary=$(find_plugin_binary)
    bundle_path=$(locate_or_create_bundle "$plugin_binary")
    verify_binary_portability "$bundle_path"
    install_plugin "$bundle_path"
    verify_installation
    print_summary "$bundle_path"
}

# Run main function
main "$@"