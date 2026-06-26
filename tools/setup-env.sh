#!/bin/bash

# AIFX Development Environment Setup Script
#
# Automates the initial setup of the AIFX plugin build environment:
# installs Conan, creates the Conan default profile, and creates the
# standard OFX plugin directories. After this, build plugins with
# tools/build-plugin.sh.
# Supports Linux, macOS, and Windows (Git Bash/MSYS2)

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
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

# Detect platform
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
    log_success "Detected platform: $PLATFORM"
}

# Ensure we're running from the AIFX repository root. setup_env_vars records
# REPO_ROOT as $(pwd), and the build scripts resolve plugin paths relative to
# it, so the script must be invoked from the checkout root.
check_aifx_root() {
    if [[ ! -f "CMakeLists.txt" ]] || [[ ! -d "plugins" ]]; then
        log_error "Not in the AIFX root directory."
        log_info "cd into your aifx checkout and re-run: ./tools/setup-env.sh"
        exit 1
    fi
    log_success "AIFX repository root detected: $(pwd)"
}

# Check for required tools
check_prerequisites() {
    log_info "Checking prerequisites..."

    # Check for Git
    if ! command -v git &> /dev/null; then
        case "$PLATFORM" in
            macos)
                log_error "Git not found. Please install Xcode Command Line Tools:"
                echo "xcode-select --install"
                ;;
            linux)
                log_error "Git not found. Please install git:"
                echo "sudo apt-get install git   # Ubuntu/Debian"
                echo "sudo yum install git      # RHEL/CentOS"
                echo "sudo dnf install git      # Fedora"
                ;;
            windows)
                log_error "Git not found. Please install Git for Windows:"
                echo "https://git-scm.com/download/win"
                ;;
        esac
        exit 1
    fi

    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        case "$PLATFORM" in
            macos)
                log_warning "CMake not found. Install with: brew install cmake"
                read -p "Install CMake now? (y/n): " -n 1 -r
                echo
                if [[ $REPLY =~ ^[Yy]$ ]]; then
                    if command -v brew &> /dev/null; then
                        brew install cmake
                    else
                        log_error "Homebrew not found. Please install CMake manually."
                        exit 1
                    fi
                else
                    exit 1
                fi
                ;;
            linux)
                log_error "CMake not found. Please install cmake:"
                echo "sudo apt-get install cmake   # Ubuntu/Debian"
                echo "sudo yum install cmake       # RHEL/CentOS"
                echo "sudo dnf install cmake       # Fedora"
                exit 1
                ;;
            windows)
                log_error "CMake not found. Please install CMake:"
                echo "https://cmake.org/download/"
                exit 1
                ;;
        esac
    fi

    # Check for Python 3
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 not found. Please install Python 3.8+"
        exit 1
    fi

    log_success "Prerequisites check passed"
}

# Ensure the STATIC C++ runtime is installed (Linux only).
#
# Each .ofx links libstdc++/libgcc statically (-static-libstdc++ -static-libgcc,
# set in plugins/CMakeLists.txt) so the plugin carries its own std::filesystem /
# std::string and can't bind to the host's. DaVinci Resolve loads libProResRAW.so
# — which re-exports an older statically-linked libstdc++ — into the global symbol
# scope; a dynamically-linked plugin then binds std::filesystem to that stale copy
# and SIGSEGVs the first time it touches the filesystem (Collect & Submit).
#
# Static linkage needs libstdc++.a, which on Debian/Ubuntu ships with the g++ dev
# package (build-essential) but on Rocky/RHEL/Fedora is a separate package
# (libstdc++-static) living in the CRB repo, NOT installed by default.
check_cxx_runtime() {
    [[ "$PLATFORM" != "linux" ]] && return 0

    log_info "Checking C++ build toolchain (compiler + static runtime)..."

    # Compiler first.
    CXX_BIN="$(command -v g++ || command -v clang++ || true)"
    if [[ -z "$CXX_BIN" ]]; then
        log_error "No C++ compiler (g++/clang++) found."
        echo "  Debian/Ubuntu: sudo apt-get install build-essential"
        echo "  Rocky/RHEL:    sudo dnf install gcc-c++"
        echo "  Fedora:        sudo dnf install gcc-c++"
        exit 1
    fi

    # Probe a real static-libstdc++ link — the only reliable test.
    if echo 'int main(){}' | "$CXX_BIN" -static-libstdc++ -static-libgcc -x c++ - -o /dev/null 2>/dev/null; then
        log_success "Static C++ runtime present (-static-libstdc++ links)"
        return 0
    fi

    log_warning "Static C++ runtime (libstdc++.a) missing — plugins will crash in DaVinci Resolve without it."

    # Pick an installer by package manager.
    if command -v dnf &> /dev/null; then
        log_info "On Rocky/RHEL/Fedora this is the 'libstdc++-static' package (Rocky/RHEL: in the CRB repo)."
        read -p "Install it now with dnf (may enable the CRB repo)? (y/n): " -n 1 -r; echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            # CRB only exists on RHEL-clones (Rocky/Alma); on Fedora the package is in the main repo.
            sudo dnf config-manager --set-enabled crb 2>/dev/null || true
            sudo dnf install -y libstdc++-static
        fi
    elif command -v apt-get &> /dev/null; then
        log_info "On Debian/Ubuntu the static lib ships with the g++ dev package."
        read -p "Install build-essential now with apt-get? (y/n): " -n 1 -r; echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            sudo apt-get update && sudo apt-get install -y build-essential
        fi
    else
        log_error "Unknown package manager — install the static libstdc++ manually (libstdc++-static / static g++ dev)."
    fi

    # Re-probe; fail hard if still unavailable so the dev hits this now, not at link time.
    if echo 'int main(){}' | "$CXX_BIN" -static-libstdc++ -static-libgcc -x c++ - -o /dev/null 2>/dev/null; then
        log_success "Static C++ runtime now available"
    else
        log_error "Static C++ runtime still missing. Install it, then re-run setup."
        echo "  Rocky/RHEL: sudo dnf config-manager --set-enabled crb && sudo dnf install libstdc++-static"
        echo "  Ubuntu:     sudo apt-get install build-essential"
        exit 1
    fi
}

# Install Conan
setup_conan() {
    log_info "Setting up Conan package manager..."

    # Check if Conan is already installed
    if command -v conan &> /dev/null; then
        CONAN_VERSION=$(conan --version | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+')
        log_info "Conan $CONAN_VERSION already installed"
    else
        log_info "Installing Conan..."
        python3 -m pip install 'conan>=2.1.0'

        # Find Conan installation path
        CONAN_PATH=$(python3 -c "import sys; import os; print(os.path.join(sys.prefix, 'bin'))")
        export PATH="$PATH:$CONAN_PATH"

        if ! command -v conan &> /dev/null; then
            # Try alternative paths
            for potential_path in ~/.local/bin ~/Library/Python/*/bin ~/.pyenv/versions/*/bin; do
                if [[ -f "$potential_path/conan" ]]; then
                    export PATH="$PATH:$potential_path"
                    break
                fi
            done
        fi
    fi

    # Create default profile. build-plugin.sh runs `conan install -pr:b=default`
    # on every build, so a default profile must exist before any plugin builds.
    if [[ ! -f ~/.conan2/profiles/default ]]; then
        log_info "Creating Conan default profile..."
        conan profile detect
        log_success "Conan profile created"
    else
        log_info "Conan profile already exists"
    fi
}

# Get platform-specific plugin paths
get_plugin_paths() {
    case "$PLATFORM" in
        macos)
            USER_PLUGIN_DIR="$HOME/Library/OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
        linux)
            USER_PLUGIN_DIR="$HOME/OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
        windows)
            USER_PLUGIN_DIR="$HOME/AppData/Roaming/OFX/Plugins"
            DEV_PLUGIN_DIR="$HOME/OFX/Plugins"
            ;;
    esac
}

# Setup directories
setup_directories() {
    log_info "Setting up directories..."

    get_plugin_paths

    # Create user and development plugin directories
    mkdir -p "$USER_PLUGIN_DIR"
    mkdir -p "$DEV_PLUGIN_DIR"

    log_success "Plugin directories created:"
    log_info "  User: $USER_PLUGIN_DIR"
    log_info "  Development: $DEV_PLUGIN_DIR"
}

# Setup environment variables
setup_env_vars() {
    log_info "Setting up environment variables..."

    SHELL_RC=""
    if [[ -f ~/.zshrc ]]; then
        SHELL_RC=~/.zshrc
    elif [[ -f ~/.bash_profile ]]; then
        SHELL_RC=~/.bash_profile
    elif [[ -f ~/.bashrc ]]; then
        SHELL_RC=~/.bashrc
    else
        log_warning "No shell RC file found. Creating ~/.zshrc"
        touch ~/.zshrc
        SHELL_RC=~/.zshrc
    fi

    # Check if AIFX environment is already configured
    if grep -q "REPO_ROOT" "$SHELL_RC"; then
        log_info "AIFX environment variables already configured"
    else
        log_info "Adding AIFX environment variables to $SHELL_RC"

        cat >> "$SHELL_RC" << EOF

# AIFX Development Environment
export REPO_ROOT="$(pwd)"
export OFX_PLUGIN_PATH="$USER_PLUGIN_DIR"
export OFX_DEV_PLUGIN_PATH="$DEV_PLUGIN_DIR"

# Add Conan to PATH
export PATH="\$PATH:\$(python3 -c 'import sys; import os; print(os.path.join(sys.prefix, "bin"))')"

# Helper aliases
alias ofx-plugins="find \$OFX_PLUGIN_PATH -maxdepth 1 -name '*.ofx.bundle' -type d -exec ls -ld {} + 2>/dev/null || echo 'No plugins found'"
alias ofx-dev-build="\$REPO_ROOT/tools/build-plugin.sh"

EOF
        log_success "Environment variables added to $SHELL_RC"
        log_warning "Please run: source $SHELL_RC"
    fi
}

# Verify the environment is ready to build
verify_setup() {
    log_info "Verifying environment..."

    if command -v conan &> /dev/null; then
        log_success "Conan available: $(conan --version)"
    else
        log_warning "Conan not on PATH yet. Reload your shell (source your RC file) or restart the terminal."
    fi

    if [[ -f ~/.conan2/profiles/default ]]; then
        log_success "Conan default profile present (~/.conan2/profiles/default)"
    else
        log_warning "Conan default profile missing. Run: conan profile detect"
    fi

    get_plugin_paths
    if [[ -d "$USER_PLUGIN_DIR" ]]; then
        log_success "OFX plugin directory ready: $USER_PLUGIN_DIR"
    else
        log_warning "OFX plugin directory missing: $USER_PLUGIN_DIR"
    fi

    log_success "Environment verification completed"
}

# Print next steps
print_next_steps() {
    log_success "AIFX development environment setup complete!"
    echo
    get_plugin_paths
    echo "Next steps:"
    echo "1. Reload your shell: source ~/.zshrc  (or ~/.bash_profile)"
    echo "2. Build and install a plugin, e.g.:"
    echo "     ./tools/build-plugin.sh plugins/depth_da3 DepthAnything3 --install"
    echo "3. Find installed plugins in: $USER_PLUGIN_DIR"
    echo
    echo "Available commands (after reloading your shell):"
    echo "  ofx-dev-build    - Build a specific plugin (tools/build-plugin.sh)"
    echo "  ofx-plugins      - List installed plugins"
    echo
    echo "Documentation: docs/installation.md (Building from source)"
}

# Main execution
main() {
    echo "AIFX Development Environment Setup"
    echo "====================================="
    echo

    detect_platform
    check_aifx_root
    check_prerequisites
    check_cxx_runtime
    setup_conan
    setup_directories
    setup_env_vars
    verify_setup
    print_next_steps
}

# Run main function
main "$@"
