# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Build all AIFX plugins for Windows x86_64 and package them into a release
# zip archive. Patterned on tools/release-macos.sh.
#
# Usage: tools\release-windows.ps1 [-Version <version>]
#   Version defaults to "dev" if not supplied.

[CmdletBinding()]
param(
    [string]$Version = "dev"
)

$ErrorActionPreference = "Stop"
$Arch = "x86_64"

$ScriptDir = Split-Path -Parent $PSCommandPath
$RepoRoot  = Resolve-Path (Join-Path $ScriptDir "..")
Set-Location $RepoRoot

# Plugin set comes from plugins/manifest.txt (single source of truth).
# Each entry is a (plugin-dir, target) pair; downstream code uses $entry[0]/$entry[1].
$ManifestPath = Join-Path $RepoRoot "plugins\manifest.txt"
if (-not (Test-Path $ManifestPath)) {
    Write-Host "[ERROR] plugin manifest not found: $ManifestPath" -ForegroundColor Red
    exit 1
}
$Targets = @(
    foreach ($line in Get-Content $ManifestPath) {
        $trimmed = $line.Trim()
        if ($trimmed -eq "" -or $trimmed.StartsWith("#")) { continue }
        $parts = $trimmed -split '\s+'
        # Unary comma keeps each entry a 2-element array (not flattened).
        ,@($parts[0], $parts[1])
    }
)

function Write-Step($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Write-Err($msg)  { Write-Host "[ERROR] $msg" -ForegroundColor Red }

Write-Step "Building all $($Targets.Count) AIFX plugins for Windows $Arch..."
foreach ($entry in $Targets) {
    $pluginDir = $entry[0]
    $target    = $entry[1]
    Write-Host ""
    Write-Step "$target ($pluginDir)"
    & "$ScriptDir\build-windows-plugin.ps1" "plugins/$pluginDir" $target
    if ($LASTEXITCODE -ne 0) {
        Write-Err "Build failed for $target"
        exit 1
    }
}

Write-Step "Verifying bundles..."
foreach ($entry in $Targets) {
    $target = $entry[1]
    $bundle = "build\windows\${target}.ofx.bundle"
    $bin    = "${bundle}\Contents\Win64-AMD64\${target}.ofx"
    if (-not (Test-Path $bin)) {
        Write-Err "Missing bundle binary: $bin"
        exit 1
    }
    Write-Ok $target
}

Write-Step "Packaging..."
$Top   = "AIFX-${Version}-windows-${Arch}"
$Stage = "dist\staging\${Top}"
if (Test-Path "dist\staging") { Remove-Item -Recurse -Force "dist\staging" }
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

foreach ($entry in $Targets) {
    $target = $entry[1]
    Copy-Item -Recurse "build\windows\${target}.ofx.bundle" -Destination $Stage
}

$ReadmePath = Join-Path $Stage "README.txt"
@"
AIFX ${Version} -- Windows ${Arch}
====================================================

This archive contains seven OpenFX plugin bundles built for Windows ${Arch}.

To install:

  1. Copy every *.ofx.bundle directory in this archive into the standard
     OFX plugin directory for Windows:

       C:\Program Files\Common Files\OFX\Plugins\

  2. Restart your OFX host (Nuke, Fusion, Flame, Resolve, ...).
     Plugins appear under the AIFX category in the effect/filter browser.

The plugins require a running ComfyUI server with matching custom nodes.
See the documentation:

  https://dev-reepost.github.io/aifx/comfyui-server-setup/

Plugin code:   BSD-3-Clause.
Model weights: per upstream, see each plugin page.
Authors:       MaGMa for Reepost Studio
               https://www.reepoststudio.fr/
Funding:       CNC (Centre national du cinema et de l'image animee).

Status: pre-release. Plugins are functional but undergoing testing in
production hosts.
"@ | Out-File -FilePath $ReadmePath -Encoding utf8

if (-not (Test-Path "dist")) { New-Item -ItemType Directory -Force -Path "dist" | Out-Null }
$ZipPath = "dist\aifx-${Version}-windows-${Arch}.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path (Join-Path "dist\staging" $Top) -DestinationPath $ZipPath
Remove-Item -Recurse -Force "dist\staging"

Write-Step "Release artifact:"
Get-Item $ZipPath | Format-List Name, Length, LastWriteTime

Write-Step "SHA-256 for release notes:"
Get-FileHash -Algorithm SHA256 $ZipPath | Format-List Hash, Path

Write-Step "Next steps (once macOS + Linux tarballs also exist):"
Write-Host "    1. Tag the commit:    git tag -a v${Version} -m `"AIFX v${Version}`""
Write-Host "    2. Push the tag:      git push origin v${Version}"
Write-Host "    3. Create release:    gh release create v${Version} `"$ZipPath`" --prerelease"
Write-Host "       (or do it via the GitHub web UI)"
