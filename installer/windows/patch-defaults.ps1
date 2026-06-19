# SPDX-License-Identifier: BSD-3-Clause
# Copyright AIFX contributors.
#
# Rewrite every installed plugin bundle's defaults.json with the site config
# collected by the Inno Setup wizard (installer/windows/aifx-installer.iss).
#
# Invoked post-install:
#   powershell -NoProfile -ExecutionPolicy Bypass -File patch-defaults.ps1 `
#     -PluginsDir "<OFX plugins dir>" -ConfigFile "<tmp>\aifx-site-config.txt"
#
# The config file is a UTF-8 key=value list (one per line):
#   serverAddress / serverPort / localMountPath / serverMountPath /
#   timeout / enableProcessing
#
# This must run under Windows PowerShell 5.1 (what end users have), so it sticks
# to PSCustomObject manipulation rather than ConvertFrom-Json -AsHashtable
# (PS 6+ only). The keys written match what the plugin actually reads at runtime
# (plugins/common/comfyui_base_plugin.cpp): server.{serverAddress,serverPort},
# storage.serverMountPath, storage.localMountPath.windows,
# controls.{timeout,enableProcessing}. Any keys we don't touch (the per-plugin
# project block, the macos/linux mount entries, enableCache, asyncMode, ...) are
# preserved.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PluginsDir,
    [Parameter(Mandatory = $true)][string]$ConfigFile
)

$ErrorActionPreference = "Stop"

function Read-ConfigFile([string]$Path) {
    $cfg = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^\s*#' -or $line.Trim() -eq '') { continue }
        $idx = $line.IndexOf('=')
        if ($idx -lt 1) { continue }
        $key = $line.Substring(0, $idx).Trim()
        $val = $line.Substring($idx + 1)   # keep value verbatim (paths, spaces)
        $cfg[$key] = $val.TrimEnd("`r", "`n")
    }
    return $cfg
}

# Set a (possibly missing) property on a PSCustomObject.
function Set-Prop($obj, [string]$name, $value) {
    if ($obj.PSObject.Properties.Match($name).Count -gt 0) {
        $obj.$name = $value
    } else {
        $obj | Add-Member -NotePropertyName $name -NotePropertyValue $value
    }
}

# Return obj.name, creating it as an empty object if missing/null.
function Get-OrAddObject($obj, [string]$name) {
    if (($obj.PSObject.Properties.Match($name).Count -eq 0) -or ($null -eq $obj.$name)) {
        Set-Prop $obj $name ([PSCustomObject]@{})
    }
    return $obj.$name
}

function Write-JsonNoBom([string]$Path, $Object) {
    $json = $Object | ConvertTo-Json -Depth 20
    # nlohmann tolerates a BOM, but writing without one keeps the files byte-for-
    # byte editable and matches the build-time merge output.
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $json, $utf8NoBom)
}

if (-not (Test-Path -LiteralPath $PluginsDir)) {
    Write-Error "Plugins directory not found: $PluginsDir"
    exit 1
}
if (-not (Test-Path -LiteralPath $ConfigFile)) {
    Write-Error "Config file not found: $ConfigFile"
    exit 1
}

$cfg = Read-ConfigFile $ConfigFile

# Normalise typed values.
[int]$serverPort = 8188
if ($cfg.ContainsKey('serverPort')) { [void][int]::TryParse($cfg['serverPort'], [ref]$serverPort) }
[int]$timeout = 600
if ($cfg.ContainsKey('timeout')) { [void][int]::TryParse($cfg['timeout'], [ref]$timeout) }
$enableProcessing = ($cfg['enableProcessing'] -eq '1')
$serverAddress  = if ($cfg.ContainsKey('serverAddress'))  { $cfg['serverAddress'] }  else { '' }
$localMountPath = if ($cfg.ContainsKey('localMountPath')) { $cfg['localMountPath'] } else { '' }
$serverMountPath = if ($cfg.ContainsKey('serverMountPath')) { $cfg['serverMountPath'] } else { '' }

$bundles = Get-ChildItem -LiteralPath $PluginsDir -Directory -Filter '*.ofx.bundle' -ErrorAction SilentlyContinue
if (-not $bundles -or $bundles.Count -eq 0) {
    Write-Error "No .ofx.bundle directories found under $PluginsDir"
    exit 1
}

$patched = 0
foreach ($bundle in $bundles) {
    $file = Join-Path $bundle.FullName 'Contents\Resources\config\defaults.json'
    if (-not (Test-Path -LiteralPath $file)) {
        Write-Host "  (no defaults.json in $($bundle.Name) - skipping)"
        continue
    }

    try {
        $raw = Get-Content -LiteralPath $file -Raw
        $root = $raw | ConvertFrom-Json
    } catch {
        Write-Warning "Skipping $($bundle.Name): could not parse defaults.json ($_)"
        continue
    }

    $server = Get-OrAddObject $root 'server'
    Set-Prop $server 'serverAddress' $serverAddress
    Set-Prop $server 'serverPort'    $serverPort

    $storage = Get-OrAddObject $root 'storage'
    Set-Prop $storage 'serverMountPath' $serverMountPath
    $localMount = Get-OrAddObject $storage 'localMountPath'
    # Only seed the Windows view; leave any bundled macos/linux entries intact.
    # A blank local mount is valid — the plugin falls back to serverMountPath.
    Set-Prop $localMount 'windows' $localMountPath

    $controls = Get-OrAddObject $root 'controls'
    Set-Prop $controls 'timeout'          $timeout
    Set-Prop $controls 'enableProcessing' $enableProcessing

    Write-JsonNoBom $file $root
    Write-Host "  patched $($bundle.Name)"
    $patched++
}

Write-Host "Wrote site config into $patched bundle(s) under $PluginsDir"
if ($patched -eq 0) { exit 1 }
exit 0
