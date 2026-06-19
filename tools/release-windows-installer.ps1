# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build the AIFX Windows installer (Setup.exe) with Inno Setup.
#
# The Windows counterpart of tools/release-macos-installer.sh. It:
#   1. Stages the seven .ofx.bundle payloads into installer\windows\Bundles\
#      (preferring the release zip dist\aifx-<version>-windows-x86_64.zip for
#      reproducibility, falling back to build\windows\*.ofx.bundle for dev).
#   2. Verifies every bundle carries its binary + config\defaults.json.
#   3. Compiles installer\windows\aifx-installer.iss with ISCC, baking VERSION
#      in via /DAppVersion.
#   4. Optionally code-signs the resulting Setup.exe (if signing env is set).
#   5. Prints SHA-256 and the gh release upload command.
#
# Output:
#   dist\aifx-<version>-windows-setup.exe
#
# Requirements:
#   - Inno Setup 6 (ISCC.exe). Install from https://jrsoftware.org/isdl.php
#     or:  winget install JRSoftware.InnoSetup
#
# Optional signing (skipped cleanly if unset):
#   $env:AIFX_SIGN_THUMBPRINT  - cert thumbprint in the local cert store, or
#   $env:AIFX_SIGN_PFX + $env:AIFX_SIGN_PFX_PASSWORD - path to a .pfx + password
#   $env:AIFX_TIMESTAMP_URL     - RFC3161 timestamp URL
#                                 (default http://timestamp.digicert.com)
#
# Usage: tools\release-windows-installer.ps1 [-Version <version>]

[CmdletBinding()]
param(
    [string]$Version = "dev"
)

$ErrorActionPreference = "Stop"
$Arch = "x86_64"

$ScriptDir = Split-Path -Parent $PSCommandPath
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir "..")).Path
Set-Location $RepoRoot

function Write-Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Write-Err($msg)  { Write-Host "[ERROR] $msg" -ForegroundColor Red }

$InstallerDir = Join-Path $RepoRoot "installer\windows"
$IssPath      = Join-Path $InstallerDir "aifx-installer.iss"
$StageDir     = Join-Path $InstallerDir "Bundles"
$DistDir      = Join-Path $RepoRoot "dist"
$ZipPath      = Join-Path $DistDir "aifx-${Version}-windows-${Arch}.zip"
$SetupName    = "aifx-${Version}-windows-setup.exe"
$SetupPath    = Join-Path $DistDir $SetupName

if (-not (Test-Path $IssPath)) {
    Write-Err "Inno Setup script not found: $IssPath"
    exit 1
}

# Plugin set comes from plugins/manifest.txt (single source of truth).
$ManifestPath = Join-Path $RepoRoot "plugins\manifest.txt"
if (-not (Test-Path $ManifestPath)) {
    Write-Err "plugin manifest not found: $ManifestPath"
    exit 1
}
$Targets = @(
    foreach ($line in Get-Content $ManifestPath) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
        ($trimmed -split '\s+')[1]
    }
)

if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Force -Path $DistDir | Out-Null }

# --- 1. Stage the seven bundle payloads --------------------------------------

Write-Step "[1/4] Staging .ofx.bundle payloads..."
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

if (Test-Path $ZipPath) {
    Write-Host "    using release zip: $ZipPath"
    $tmpExtract = Join-Path ([System.IO.Path]::GetTempPath()) ("aifx-stage-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmpExtract | Out-Null
    try {
        Expand-Archive -Path $ZipPath -DestinationPath $tmpExtract -Force
        # The zip contains AIFX-<version>-windows-<arch>\*.ofx.bundle.
        $top = Get-ChildItem -Path $tmpExtract -Directory | Select-Object -First 1
        if (-not $top) { Write-Err "Could not find the top directory inside $ZipPath"; exit 1 }
        Get-ChildItem -Path $top.FullName -Directory -Filter '*.ofx.bundle' | ForEach-Object {
            Copy-Item -Recurse -Force $_.FullName -Destination $StageDir
        }
    } finally {
        Remove-Item -Recurse -Force $tmpExtract -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "    zip missing; staging from build\windows\*.ofx.bundle"
    $buildWin = Join-Path $RepoRoot "build\windows"
    if (-not (Test-Path $buildWin)) {
        Write-Err "No build\windows and no $ZipPath. Run tools\release-windows.ps1 -Version $Version first."
        exit 1
    }
    Get-ChildItem -Path $buildWin -Directory -Filter '*.ofx.bundle' | ForEach-Object {
        # Drop any stale top-level Resources\ leftover (see release-windows.ps1).
        $copy = Copy-Item -Recurse -Force $_.FullName -Destination $StageDir -PassThru
    }
    Get-ChildItem -Path $StageDir -Directory -Filter '*.ofx.bundle' | ForEach-Object {
        $legacy = Join-Path $_.FullName "Resources"
        if (Test-Path $legacy) { Remove-Item -Recurse -Force $legacy -ErrorAction SilentlyContinue }
    }
}

# --- 2. Verify every expected bundle is present and complete -----------------

Write-Step "[2/4] Verifying staged bundles..."
foreach ($target in $Targets) {
    $bundle  = Join-Path $StageDir "${target}.ofx.bundle"
    $bin     = Join-Path $bundle "Contents\Win64\${target}.ofx"
    $defaults = Join-Path $bundle "Contents\Resources\config\defaults.json"
    if (-not (Test-Path $bundle)) {
        Write-Err "Missing staged bundle: ${target}.ofx.bundle"
        exit 1
    }
    if (-not (Test-Path $bin)) {
        Write-Err "Missing bundle binary: $bin"
        exit 1
    }
    if (-not (Test-Path $defaults)) {
        Write-Err "Missing $defaults - bundle wasn't built with the config merge step."
        exit 1
    }
    Write-Ok $target
}

# --- 3. Compile the installer with ISCC --------------------------------------

Write-Step "[3/4] Compiling installer with Inno Setup..."

function Find-ISCC {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        # winget's per-user install location.
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
    foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
    return $null
}

$iscc = Find-ISCC
if (-not $iscc) {
    Write-Err "ISCC.exe (Inno Setup 6) not found."
    Write-Host "    Install it from https://jrsoftware.org/isdl.php"
    Write-Host "    or run: winget install JRSoftware.InnoSetup"
    exit 1
}
Write-Host "    using: $iscc"

# /O overrides OutputDir; the base filename comes from the .iss (derived from
# AppVersion). Quiet output but show errors.
& $iscc "/DAppVersion=$Version" "/O$DistDir" $IssPath
if ($LASTEXITCODE -ne 0) {
    Write-Err "Inno Setup compilation failed (exit code $LASTEXITCODE)."
    exit 1
}
if (-not (Test-Path $SetupPath)) {
    Write-Err "Expected installer not produced: $SetupPath"
    exit 1
}
Write-Ok "built $SetupName"

# --- 4. Optional code-signing ------------------------------------------------

Write-Step "[4/4] Signing..."
$signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
$timestampUrl = if ($env:AIFX_TIMESTAMP_URL) { $env:AIFX_TIMESTAMP_URL } else { "http://timestamp.digicert.com" }

if ($env:AIFX_SIGN_THUMBPRINT -or $env:AIFX_SIGN_PFX) {
    if (-not $signtool) {
        Write-Err "Signing requested but signtool.exe not found on PATH (install the Windows SDK)."
        exit 1
    }
    $signArgs = @("sign", "/fd", "SHA256", "/tr", $timestampUrl, "/td", "SHA256")
    if ($env:AIFX_SIGN_THUMBPRINT) {
        $signArgs += @("/sha1", $env:AIFX_SIGN_THUMBPRINT)
    } else {
        $signArgs += @("/f", $env:AIFX_SIGN_PFX)
        if ($env:AIFX_SIGN_PFX_PASSWORD) { $signArgs += @("/p", $env:AIFX_SIGN_PFX_PASSWORD) }
    }
    $signArgs += $SetupPath
    & $signtool @signArgs
    if ($LASTEXITCODE -ne 0) { Write-Err "signtool failed"; exit 1 }
    Write-Ok "signed $SetupName"
} else {
    Write-Host "    No signing env set (AIFX_SIGN_THUMBPRINT / AIFX_SIGN_PFX) - building UNSIGNED."
    Write-Host "    Windows SmartScreen may warn on first run. To sign, set one of:"
    Write-Host "      `$env:AIFX_SIGN_THUMBPRINT = '<cert thumbprint in cert store>'"
    Write-Host "      `$env:AIFX_SIGN_PFX = 'C:\path\to\cert.pfx'; `$env:AIFX_SIGN_PFX_PASSWORD = '...'"
}

# --- Summary -----------------------------------------------------------------

Write-Step "Release artifact:"
Get-Item $SetupPath | Format-List Name, Length, LastWriteTime

Write-Step "SHA-256 for release notes:"
Get-FileHash -Algorithm SHA256 $SetupPath | Format-List Hash, Path

Write-Step "Attach to the GitHub release:"
Write-Host "    gh release upload v${Version} `"$SetupPath`""
