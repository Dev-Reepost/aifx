# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.
#
# Build OpenFX Plugin for Windows (x64)
#
# This script builds OpenFX plugins on Windows using CMake, Conan, and MSVC.

param(
    [string]$Plugin = "AnyComfy",
    [string]$Target = "AnyComfy",
    [switch]$Clean = $false,
    [switch]$Install = $false,
    [string]$InstallDir = "",
    [string]$BuildType = "Release",
    [switch]$Verbose = $false,
    [switch]$Help = $false
)

# Error handling
$ErrorActionPreference = "Stop"

# Color output functions
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Blue
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor Yellow
}

function Write-ErrorMsg {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Show-Usage {
    Write-Host @"
Usage: .\build-windows-plugin.ps1 [options]

Build OpenFX plugins for Windows (x64).

Options:
    -Plugin NAME        Plugin name (default: AnyComfy)
    -Target NAME        CMake target name (default: AnyComfy)
    -Clean              Clean build directory before building
    -Install            Install plugin after building
    -InstallDir DIR     Custom installation directory
    -BuildType TYPE     Build type: Release (default) or Debug
    -Verbose            Show detailed build output
    -Help               Show this help message

Examples:
    .\build-windows-plugin.ps1                                    # Build AnyComfy
    .\build-windows-plugin.ps1 -Install                           # Build and install to user directory
    .\build-windows-plugin.ps1 -Plugin MyPlugin -Target MyPlugin  # Build custom plugin
    .\build-windows-plugin.ps1 -Clean -Verbose                    # Clean build with verbose output
    .\build-windows-plugin.ps1 -InstallDir "C:\OFX\Plugins"       # Install to custom directory

Requirements:
    - Python 3.8+
    - CMake 3.28+
    - Conan 2.1+
    - Visual Studio 2019+ or Build Tools with C++ workload

    Run .\contrib\dev-tools\setup-env.ps1 to install requirements.

"@
}

if ($Help) {
    Show-Usage
    exit 0
}

# ============================================================================
# Global Variables
# ============================================================================

$BUILD_DIR = "build\windows"
$BUNDLE_NAME = "$Target.ofx.bundle"
$BINARY_NAME = "$Target.ofx"

# Set default install directory
if ($Install -and [string]::IsNullOrEmpty($InstallDir)) {
    $InstallDir = "$env:USERPROFILE\OFX\Plugins"
}

# ============================================================================
# Helper Functions
# ============================================================================

function Get-OpenFXRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    $root = Resolve-Path (Join-Path $scriptDir "..\..") -ErrorAction Stop
    return $root.Path
}

function Test-Requirements {
    Write-Info "Verifying build requirements..."

    # Check Python
    try {
        $null = & python --version 2>&1
        if ($LASTEXITCODE -ne 0) { throw }
        Write-Success "Python: OK"
    } catch {
        Write-ErrorMsg "Python not found. Install from https://www.python.org/"
        exit 1
    }

    # Check CMake
    try {
        $null = & cmake --version 2>&1
        if ($LASTEXITCODE -ne 0) { throw }
        Write-Success "CMake: OK"
    } catch {
        Write-ErrorMsg "CMake not found. Install from https://cmake.org/"
        Write-Info "Or run: .\contrib\dev-tools\setup-env.ps1"
        exit 1
    }

    # Check Conan
    try {
        $null = & conan --version 2>&1
        if ($LASTEXITCODE -ne 0) { throw }
        Write-Success "Conan: OK"
    } catch {
        Write-ErrorMsg "Conan not found. Install with: python -m pip install conan"
        Write-Info "Or run: .\contrib\dev-tools\setup-env.ps1"
        exit 1
    }

    # Check Visual Studio Build Tools
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
        if ($vsPath) {
            Write-Success "Visual Studio Build Tools: OK"
        } else {
            Write-Warning "Visual Studio C++ Build Tools not found"
            Write-Info "Install from: https://visualstudio.microsoft.com/downloads/"
        }
    } else {
        Write-Warning "Visual Studio not detected"
    }
}

function Clean-BuildDirectory {
    if ($Clean -and (Test-Path $BUILD_DIR)) {
        Write-Warning "Cleaning build directory: $BUILD_DIR"
        Remove-Item -Path $BUILD_DIR -Recurse -Force
        Write-Success "Build directory cleaned"
    }
}

function Install-ConanDependencies {
    Write-Info "Step 1/4: Installing Conan dependencies..."

    $conanArgs = @(
        "install", ".",
        "-s", "build_type=$BuildType",
        "-s", "arch=x86_64",
        "-pr:b=default",
        "--build=missing",
        "-o", "&:build_comfyui_plugins=True",
        "-of=$BUILD_DIR"
    )

    Write-Info "Running: conan $($conanArgs -join ' ')"

    try {
        & conan @conanArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Conan install failed with exit code $LASTEXITCODE, trying with --build=*"

            $conanArgs[7] = "--build=*"  # Replace --build=missing
            & conan @conanArgs

            if ($LASTEXITCODE -ne 0) {
                throw "Conan install failed"
            }
        }
        Write-Success "Conan dependencies installed"
    } catch {
        Write-ErrorMsg "Failed to install dependencies: $_"
        exit 1
    }
}

function Configure-CMake {
    Write-Info "Step 2/4: Configuring CMake..."

    # Find the Conan toolchain file
    $toolchainFile = Get-ChildItem -Path $BUILD_DIR -Filter "conan_toolchain.cmake" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

    if (-not $toolchainFile) {
        Write-ErrorMsg "Could not find conan_toolchain.cmake in $BUILD_DIR"
        exit 1
    }

    Write-Info "Using toolchain: $($toolchainFile.FullName)"

    $cmakeArgs = @(
        "-S", ".",
        "-B", $BUILD_DIR,
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DBUILD_COMFYUI_PLUGINS=ON",
        "-DCMAKE_TOOLCHAIN_FILE=$($toolchainFile.FullName)",
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
    )

    if ($Verbose) {
        $cmakeArgs += "--debug-output"
    }

    Write-Info "Running: cmake $($cmakeArgs -join ' ')"

    try {
        & cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed with exit code $LASTEXITCODE"
        }
        Write-Success "CMake configuration complete"
    } catch {
        Write-ErrorMsg "Failed to configure CMake: $_"
        exit 1
    }
}

function Build-Plugin {
    Write-Info "Step 3/4: Building plugin target: $Target..."

    $buildArgs = @(
        "--build", $BUILD_DIR,
        "--config", $BuildType,
        "--target", $Target,
        "--parallel"
    )

    if ($Verbose) {
        $buildArgs += "--verbose"
    }

    Write-Info "Running: cmake $($buildArgs -join ' ')"

    try {
        & cmake @buildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed with exit code $LASTEXITCODE"
        }
        Write-Success "Build complete"
    } catch {
        Write-ErrorMsg "Failed to build plugin: $_"
        exit 1
    }
}

function Find-PluginBinary {
    Write-Info "Locating plugin binary..."

    # Search for the .ofx file
    $binaryPath = Get-ChildItem -Path $BUILD_DIR -Filter $BINARY_NAME -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

    if (-not $binaryPath) {
        Write-ErrorMsg "Could not find plugin binary: $BINARY_NAME"
        Write-Info "Build directory: $BUILD_DIR"
        exit 1
    }

    Write-Success "Found plugin binary: $($binaryPath.FullName)"
    return $binaryPath.FullName
}

function Find-PluginBundle {
    Write-Info "Locating plugin bundle..."

    # Search for the .ofx.bundle directory
    $bundlePath = Get-ChildItem -Path $BUILD_DIR -Filter $BUNDLE_NAME -Directory -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1

    if (-not $bundlePath) {
        Write-Warning "Plugin bundle not found, manual bundling may be required"
        return $null
    }

    Write-Success "Found plugin bundle: $($bundlePath.FullName)"
    return $bundlePath.FullName
}

function Install-Plugin {
    param([string]$BundlePath)

    if (-not $Install) {
        return
    }

    Write-Info "Step 4/4: Installing plugin to $InstallDir..."

    # Create installation directory
    if (-not (Test-Path $InstallDir)) {
        New-Item -Path $InstallDir -ItemType Directory -Force | Out-Null
        Write-Info "Created installation directory: $InstallDir"
    }

    # Remove existing plugin if present
    $destBundle = Join-Path $InstallDir $BUNDLE_NAME
    if (Test-Path $destBundle) {
        Write-Warning "Removing existing plugin: $destBundle"
        Remove-Item -Path $destBundle -Recurse -Force
    }

    # Copy plugin bundle
    if ($BundlePath) {
        Copy-Item -Path $BundlePath -Destination $InstallDir -Recurse -Force
        Write-Success "Plugin installed to: $destBundle"
    } else {
        Write-Warning "No bundle found to install"
    }
}

function Show-Summary {
    param([string]$BinaryPath, [string]$BundlePath)

    Write-Host ""
    Write-Success "Build complete!"
    Write-Host ""
    Write-Info "Plugin binary: $BinaryPath"

    if ($BundlePath) {
        Write-Info "Plugin bundle: $BundlePath"
    }

    if (-not $Install) {
        Write-Host ""
        Write-Info "To install plugin for host applications:"
        Write-Info "  User:   $env:USERPROFILE\OFX\Plugins"
        Write-Info "  System: $env:ProgramFiles\Common Files\OFX\Plugins (requires admin)"
        Write-Host ""
        Write-Info "Or run with -Install flag to install automatically"
    }

    Write-Host ""
}

# ============================================================================
# Main Execution
# ============================================================================

function Main {
    Write-Info "Building Windows plugin: $Plugin"
    Write-Info "Build type: $BuildType"
    Write-Host ""

    # Get OpenFX root and change to it
    $ofxRoot = Get-OpenFXRoot
    Write-Info "OpenFX root: $ofxRoot"
    Set-Location $ofxRoot

    # Verify requirements
    Test-Requirements
    Write-Host ""

    # Clean if requested
    Clean-BuildDirectory

    # Build steps
    try {
        Install-ConanDependencies
        Configure-CMake
        Build-Plugin

        $binaryPath = Find-PluginBinary
        $bundlePath = Find-PluginBundle

        Install-Plugin -BundlePath $bundlePath
        Show-Summary -BinaryPath $binaryPath -BundlePath $bundlePath

    } catch {
        Write-ErrorMsg "Build failed: $_"
        exit 1
    }
}

# Run main function
Main
