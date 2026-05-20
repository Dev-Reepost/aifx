# SPDX-License-Identifier: BSD-3-Clause
# Copyright OpenFX and contributors to the OpenFX project.
#
# Windows Development Environment Setup for OpenFX Plugins
#
# This script sets up the required tools for building OpenFX plugins on Windows:
# - CMake 3.28+
# - Conan 2.1+
# - Visual Studio Build Tools (if not already installed)

param(
    [switch]$SkipCMake = $false,
    [switch]$SkipConan = $false,
    [switch]$SkipVS = $false,
    [switch]$Help = $false
)

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
Usage: .\setup-windows-env.ps1 [options]

Set up Windows development environment for OpenFX plugin development.

Options:
    -SkipCMake          Skip CMake installation check
    -SkipConan          Skip Conan installation
    -SkipVS             Skip Visual Studio Build Tools check
    -Help               Show this help message

Requirements:
    - Windows 10 or later
    - Internet connection for downloading tools
    - Administrator privileges may be required for some installations

This script will:
    1. Check for Python 3.8+ (required for Conan)
    2. Install/verify CMake 3.28+
    3. Install Conan 2.1+ via pip
    4. Check for Visual Studio Build Tools
    5. Set up OpenFX plugin directory structure

Example:
    .\setup-windows-env.ps1                 # Full setup
    .\setup-windows-env.ps1 -SkipVS         # Skip VS check

"@
}

if ($Help) {
    Show-Usage
    exit 0
}

Write-Info "OpenFX Windows Development Environment Setup"
Write-Info "============================================="
Write-Host ""

# Check if running as administrator (optional, but recommended)
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "Not running as Administrator. Some installations may require elevation."
    Write-Info "Consider running: Start-Process powershell -Verb runAs -ArgumentList '-File $PSCommandPath'"
}

# ============================================================================
# 1. Check Python Installation
# ============================================================================

Write-Info "Step 1/5: Checking Python installation..."

try {
    $pythonVersion = & python --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Python found: $pythonVersion"

        # Verify Python version is 3.8+
        if ($pythonVersion -match "Python (\d+)\.(\d+)") {
            $major = [int]$matches[1]
            $minor = [int]$matches[2]

            if ($major -lt 3 -or ($major -eq 3 -and $minor -lt 8)) {
                Write-ErrorMsg "Python 3.8+ required, found Python $major.$minor"
                Write-Info "Download from: https://www.python.org/downloads/"
                exit 1
            }
        }
    }
} catch {
    Write-ErrorMsg "Python not found in PATH"
    Write-Info "Please install Python 3.8+ from: https://www.python.org/downloads/"
    Write-Info "Make sure to check 'Add Python to PATH' during installation"
    exit 1
}

# ============================================================================
# 2. Check/Install CMake
# ============================================================================

if (-not $SkipCMake) {
    Write-Info "Step 2/5: Checking CMake installation..."

    try {
        $cmakeVersion = & cmake --version 2>&1 | Select-Object -First 1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "CMake found: $cmakeVersion"

            # Verify version is 3.28+
            if ($cmakeVersion -match "cmake version (\d+)\.(\d+)") {
                $major = [int]$matches[1]
                $minor = [int]$matches[2]

                if ($major -lt 3 -or ($major -eq 3 -and $minor -lt 28)) {
                    Write-Warning "CMake 3.28+ recommended, found $major.$minor"
                    Write-Info "Download latest from: https://cmake.org/download/"
                }
            }
        }
    } catch {
        Write-Warning "CMake not found in PATH"
        Write-Info "Installing CMake via winget (if available)..."

        try {
            & winget install Kitware.CMake
            Write-Success "CMake installed via winget"

            # Add CMake to PATH for current session and permanently
            $cmakePath = "C:\Program Files\CMake\bin"
            if (Test-Path $cmakePath) {
                $currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
                if ($currentUserPath -notlike "*$cmakePath*") {
                    Write-Info "Adding CMake to permanent PATH..."
                    [Environment]::SetEnvironmentVariable("Path", "$currentUserPath;$cmakePath", "User")
                }
                # Update current session
                $env:Path = "$cmakePath;$env:Path"
                Write-Success "CMake added to PATH"
            }
        } catch {
            Write-ErrorMsg "Could not install CMake automatically"
            Write-Info "Please install manually from: https://cmake.org/download/"
            Write-Info "Make sure to add CMake to system PATH during installation"
        }
    }
} else {
    Write-Info "Step 2/5: Skipping CMake check (--SkipCMake)"
}

# ============================================================================
# 3. Check/Install Conan
# ============================================================================

if (-not $SkipConan) {
    Write-Info "Step 3/5: Checking Conan installation..."

    try {
        $conanVersion = & conan --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Conan found: $conanVersion"

            # Verify version is 2.1+
            if ($conanVersion -match "Conan version (\d+)\.(\d+)") {
                $major = [int]$matches[1]
                $minor = [int]$matches[2]

                if ($major -lt 2 -or ($major -eq 2 -and $minor -lt 1)) {
                    Write-Warning "Conan 2.1+ required, found $major.$minor"
                    Write-Info "Upgrading Conan..."
                    & python -m pip install --upgrade "conan>=2.1"
                }
            }
        }
    } catch {
        Write-Warning "Conan not found, installing via pip..."

        try {
            & python -m pip install "conan>=2.1"
            Write-Success "Conan installed successfully"

            # Get Python Scripts path and add to PATH permanently and for current session
            $pythonScriptsPath = & python -c "import sysconfig; print(sysconfig.get_path('scripts'))" 2>&1

            if ($pythonScriptsPath -and (Test-Path $pythonScriptsPath)) {
                # Add to permanent PATH
                $currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
                if ($currentUserPath -notlike "*$pythonScriptsPath*") {
                    Write-Info "Adding Conan to permanent PATH..."
                    [Environment]::SetEnvironmentVariable("Path", "$currentUserPath;$pythonScriptsPath", "User")
                }

                # Update current session
                $env:Path = "$pythonScriptsPath;$env:Path"
                Write-Success "Conan added to PATH"
            }

            # Verify installation
            try {
                $conanVersion = & conan --version 2>&1
                if ($LASTEXITCODE -eq 0) {
                    Write-Success "Conan version: $conanVersion"
                } else {
                    Write-Warning "Conan installed but not immediately available"
                }
            } catch {
                Write-Warning "Conan installed but may need terminal restart"
            }
        } catch {
            Write-ErrorMsg "Failed to install Conan"
            Write-Info "Try manually: python -m pip install conan"
            exit 1
        }
    }

} else {
    Write-Info "Step 3/5: Skipping Conan installation (--SkipConan)"
}

# ============================================================================
# 4. Check Visual Studio Build Tools
# ============================================================================

if (-not $SkipVS) {
    Write-Info "Step 4/5: Checking Visual Studio Build Tools..."

    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsFound = $false

    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null

        if ($vsPath) {
            Write-Success "Visual Studio Build Tools found: $vsPath"

            # Check for C++ build tools
            $vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path $vcvarsPath) {
                Write-Success "C++ Build Tools available"
                $vsFound = $true
            } else {
                Write-Warning "C++ Build Tools may not be installed"
            }
        }
    }

    if (-not $vsFound) {
        Write-Warning "Visual Studio Build Tools with C++ not found"
        Write-Info "Visual Studio Build Tools is required to compile C++ code on Windows."
        Write-Host ""

        $install = Read-Host "Would you like to install Visual Studio Build Tools now? (Y/N)"

        if ($install -eq 'Y' -or $install -eq 'y') {
            Write-Info "Downloading Visual Studio Build Tools installer..."
            Write-Warning "This is a large download (~3GB) and will take several minutes"

            $installerPath = "$env:TEMP\vs_buildtools.exe"
            $installerUrl = "https://aka.ms/vs/17/release/vs_buildtools.exe"

            try {
                # Download installer
                Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath -UseBasicParsing
                Write-Success "Installer downloaded"

                Write-Info "Installing Visual Studio Build Tools with C++ workload..."
                Write-Warning "This will take 10-15 minutes. Please wait..."

                # Install with C++ workload (unattended)
                $installArgs = @(
                    "--quiet",
                    "--wait",
                    "--norestart",
                    "--nocache",
                    "--add", "Microsoft.VisualStudio.Workload.VCTools",
                    "--includeRecommended"
                )

                $process = Start-Process -FilePath $installerPath -ArgumentList $installArgs -Wait -PassThru

                if ($process.ExitCode -eq 0 -or $process.ExitCode -eq 3010) {
                    Write-Success "Visual Studio Build Tools installed successfully"

                    if ($process.ExitCode -eq 3010) {
                        Write-Warning "A restart is required to complete the installation"
                    }

                    # Clean up installer
                    Remove-Item $installerPath -Force -ErrorAction SilentlyContinue
                } else {
                    Write-Warning "Installation completed with exit code: $($process.ExitCode)"
                    Write-Info "You may need to install manually from: https://visualstudio.microsoft.com/downloads/"
                }
            } catch {
                Write-ErrorMsg "Failed to install Visual Studio Build Tools: $_"
                Write-Info "Please install manually from: https://visualstudio.microsoft.com/downloads/"
                Write-Info "Required workload: 'Desktop development with C++'"
            }
        } else {
            Write-Info "Skipping Visual Studio Build Tools installation"
            Write-Info "You can install later from: https://visualstudio.microsoft.com/downloads/"
            Write-Info "Required workload: 'Desktop development with C++'"
        }
    }
} else {
    Write-Info "Step 4/5: Skipping Visual Studio check (--SkipVS)"
}

# ============================================================================
# 4b. Configure Conan Profile (after VS installation)
# ============================================================================

if (-not $SkipConan) {
    Write-Info "Configuring Conan default profile..."

    try {
        $profileResult = & conan profile detect --force 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Conan profile configured"

            # Check if MSVC was detected
            $profilePath = "$env:USERPROFILE\.conan2\profiles\default"
            if (Test-Path $profilePath) {
                $profileContent = Get-Content $profilePath -Raw
                if ($profileContent -match "Visual Studio") {
                    Write-Success "MSVC compiler detected in Conan profile"
                } elseif ($profileContent -match "gcc|clang") {
                    Write-Warning "Conan detected a Unix compiler. If you installed VS Build Tools, restart the terminal and re-run this script."
                }
            }
        } else {
            Write-Warning "Could not detect Conan profile"
            Write-Info "You may need to run: conan profile detect --force"
        }
    } catch {
        Write-Warning "Could not configure Conan profile"
        Write-Info "You may need to run: conan profile detect --force"
    }
}

# ============================================================================
# 5. Create Plugin Directory Structure
# ============================================================================

Write-Info "Step 5/5: Setting up plugin directory structure..."

$userProfile = $env:USERPROFILE
$pluginDirs = @(
    "$env:ProgramFiles\Common Files\OFX\Plugins",  # System-wide (requires admin)
    "$userProfile\OFX\Plugins"                      # User-specific (development)
)

foreach ($dir in $pluginDirs) {
    if (-not (Test-Path $dir)) {
        try {
            New-Item -Path $dir -ItemType Directory -Force | Out-Null
            Write-Success "Created directory: $dir"
        } catch {
            Write-Warning "Could not create: $dir (may require admin privileges)"
        }
    } else {
        Write-Info "Directory exists: $dir"
    }
}

# ============================================================================
# Summary
# ============================================================================

Write-Host ""
Write-Success "Setup complete!"
Write-Host ""

# Final verification
$cmakeOk = $false
$conanOk = $false

try {
    $null = & cmake --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "✓ CMake is ready to use"
        $cmakeOk = $true
    }
} catch { }

try {
    $null = & conan --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "✓ Conan is ready to use"
        $conanOk = $true
    }
} catch { }

if (-not $cmakeOk -or -not $conanOk) {
    Write-Host ""
    Write-Warning "Some tools may need a terminal restart to be fully available."
    Write-Info "If the build fails, close and reopen PowerShell."
} else {
    Write-Host ""
    Write-Success "All tools are ready! You can start building immediately."
}

Write-Host ""
Write-Info "Next steps:"
Write-Info "  1. Navigate to your OpenFX root directory"
Write-Info "  2. Run: .\contrib\dev-tools\build-windows-plugin.ps1 -Plugin AnyComfy -Target AnyComfy -Install"
Write-Host ""
Write-Info "Plugin installation directories:"
Write-Info "  Development:  $userProfile\OFX\Plugins"
Write-Info "  System-wide:  $env:ProgramFiles\Common Files\OFX\Plugins"
Write-Host ""
