# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection - Release Packaging & SHA-256 Manifest Script (Package.ps1)
param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $ProjectRoot
$BuildDir = Join-Path $WorkspaceRoot "Build\Windows"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $WorkspaceRoot "Distribution\Uncharted"
}

$CodexMsys = Join-Path $env:USERPROFILE ".codex\tmp\uncharted-build\msys64\clang64\bin"
$CodexUsr = Join-Path $env:USERPROFILE ".codex\tmp\uncharted-build\msys64\usr\bin"
if (Test-Path $CodexMsys) {
    $env:PATH = "$CodexMsys;$CodexUsr;$env:PATH"
} elseif (Test-Path "C:\msys64\clang64\bin") {
    $env:PATH = "C:\msys64\clang64\bin;C:\msys64\usr\bin;$env:PATH"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$MainExe = Join-Path $ProjectRoot "Uncharted - Sic Parvis Magna Collection.exe"
$StartExe = Join-Path $ProjectRoot "Start Uncharted.exe"

if (Test-Path $MainExe) {
    Copy-Item -Path $MainExe -Destination (Join-Path $OutputDir "Uncharted - Sic Parvis Magna Collection.exe") -Force
    Copy-Item -Path $MainExe -Destination (Join-Path $WorkspaceRoot "Uncharted - Sic Parvis Magna Collection.exe") -Force -ErrorAction SilentlyContinue
}
if (Test-Path $StartExe) {
    Copy-Item -Path $StartExe -Destination (Join-Path $OutputDir "Start Uncharted.exe") -Force
}

$ProbeExe = Join-Path $BuildDir "gow3-probe.exe"
$GpuDll = Join-Path $BuildDir "libgow3gpu.dll"
$GpuCapExe = Join-Path $BuildDir "gow3-gpu-capabilities.exe"
$PlayExe = Join-Path $BuildDir "gow3-play.exe"

$Gow3OutDir = Join-Path $ProjectRoot "gow3_pc\out"
$Gow3OutGpuDir = Join-Path $ProjectRoot "gow3_pc\out\gpu"
$DistGow3OutDir = Join-Path $OutputDir "gow3_pc\out"
$DistGow3OutGpuDir = Join-Path $OutputDir "gow3_pc\out\gpu"
$DistGow3ScriptsDir = Join-Path $OutputDir "gow3_pc\scripts"

New-Item -ItemType Directory -Force -Path $Gow3OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $Gow3OutGpuDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistGow3OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistGow3OutGpuDir | Out-Null
New-Item -ItemType Directory -Force -Path $DistGow3ScriptsDir | Out-Null

if (Test-Path $ProbeExe) {
    Copy-Item -Path $ProbeExe -Destination (Join-Path $OutputDir "gow3-probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $OutputDir "Uncharted.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $OutputDir "uncharted_probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $Gow3OutDir "gow3-probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $Gow3OutDir "Uncharted.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $DistGow3OutDir "gow3-probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $DistGow3OutDir "Uncharted.exe") -Force

    # Bundle all required MSYS2 clang64 runtime DLLs (SDL3.dll, avcodec-63.dll, avutil-61.dll, swscale-10.dll, etc.)
    $LddCmd = Get-Command ldd.exe -ErrorAction SilentlyContinue
    $ClangBinDir = if (Test-Path $CodexMsys) { $CodexMsys } elseif (Test-Path "C:\msys64\clang64\bin") { "C:\msys64\clang64\bin" } else { $null }
    if ($LddCmd -and $ClangBinDir) {
        $DllNames = & $LddCmd.Source $ProbeExe | ForEach-Object {
            if ($_ -match "=> /clang64/bin/([^\s]+\.dll)") { $matches[1] }
        } | Sort-Object -Unique
        foreach ($Dll in $DllNames) {
            $SrcDll = Join-Path $ClangBinDir $Dll
            if (Test-Path $SrcDll) {
                Copy-Item -Path $SrcDll -Destination (Join-Path $Gow3OutDir $Dll) -Force
                Copy-Item -Path $SrcDll -Destination (Join-Path $BuildDir $Dll) -Force
                Copy-Item -Path $SrcDll -Destination (Join-Path $OutputDir $Dll) -Force
                Copy-Item -Path $SrcDll -Destination (Join-Path $DistGow3OutDir $Dll) -Force
            }
        }
    }
}
if (Test-Path $GpuDll) {
    Copy-Item -Path $GpuDll -Destination (Join-Path $OutputDir "libgow3gpu.dll") -Force
    Copy-Item -Path $GpuDll -Destination (Join-Path $Gow3OutDir "libgow3gpu.dll") -Force
    Copy-Item -Path $GpuDll -Destination (Join-Path $DistGow3OutDir "libgow3gpu.dll") -Force
}
if (Test-Path $GpuCapExe) {
    Copy-Item -Path $GpuCapExe -Destination (Join-Path $OutputDir "gow3-gpu-capabilities.exe") -Force
    Copy-Item -Path $GpuCapExe -Destination (Join-Path $Gow3OutGpuDir "gow3-gpu-capabilities.exe") -Force
    Copy-Item -Path $GpuCapExe -Destination (Join-Path $DistGow3OutGpuDir "gow3-gpu-capabilities.exe") -Force
}
if (Test-Path $PlayExe) {
    Copy-Item -Path $PlayExe -Destination (Join-Path $OutputDir "gow3-play.exe") -Force
    Copy-Item -Path $PlayExe -Destination (Join-Path $Gow3OutGpuDir "gow3-play.exe") -Force
    Copy-Item -Path $PlayExe -Destination (Join-Path $DistGow3OutGpuDir "gow3-play.exe") -Force
}

if (Test-Path (Join-Path $ProjectRoot "gow3_pc\scripts")) {
    Copy-Item -Path (Join-Path $ProjectRoot "gow3_pc\scripts\*.py") -Destination $DistGow3ScriptsDir -Force
}

$DistLauncherDir = Join-Path $OutputDir "launcher"
if (Test-Path $DistLauncherDir) {
    Remove-Item -Path $DistLauncherDir -Recurse -Force
}
Copy-Item -Path (Join-Path $ProjectRoot "launcher") -Destination $DistLauncherDir -Recurse -Force

$DistToolsDir = Join-Path $OutputDir "tools"
New-Item -ItemType Directory -Force -Path $DistToolsDir | Out-Null
foreach ($toolFile in @("probe_uncharted.py", "hang_report.py", "apply_update.py", "install_update.py", "net_overlay.py", "check_game_files.py", "game_files_102.json.gz")) {
    $srcTool = Join-Path $ProjectRoot "tools\$toolFile"
    if (Test-Path $srcTool) {
        Copy-Item -Path $srcTool -Destination (Join-Path $DistToolsDir $toolFile) -Force
    }
}
if (Test-Path (Join-Path $ProjectRoot "tools\pkgtool")) {
    $DistPkgTool = Join-Path $DistToolsDir "pkgtool"
    New-Item -ItemType Directory -Force -Path $DistPkgTool | Out-Null
    Copy-Item -Path (Join-Path $ProjectRoot "tools\pkgtool\*") -Destination $DistPkgTool -Force
}
if (Test-Path (Join-Path $ProjectRoot "tools\VERSION")) {
    Copy-Item -Path (Join-Path $ProjectRoot "tools\VERSION") -Destination (Join-Path $OutputDir "VERSION") -Force
}
if (Test-Path (Join-Path $ProjectRoot "tools\defaults\disabled-optimizations.txt")) {
    $DistUserDir = Join-Path $OutputDir "analysis\uncharted-user"
    New-Item -ItemType Directory -Force -Path $DistUserDir | Out-Null
    Copy-Item -Path (Join-Path $ProjectRoot "tools\defaults\disabled-optimizations.txt") -Destination (Join-Path $DistUserDir "disabled-optimizations.txt") -Force
}

# Generate SHA-256 integrity manifest (Section 6.1 requirement)
$HashLines = @()
Get-ChildItem -Path $OutputDir -File | Sort-Object Name | ForEach-Object {
    $h = Get-FileHash -Path $_.FullName -Algorithm SHA256
    $HashLines += "$($h.Hash.ToLowerInvariant())  $($_.Name)"
}
$ShaFile = Join-Path $OutputDir "SHA256SUMS.txt"
$HashLines | Set-Content -Path $ShaFile -Encoding UTF8

Write-Host "[Package] Packaged Uncharted: Sic Parvis Magna Collection (with all runtime DLLs) and SHA256SUMS.txt into: $OutputDir" -ForegroundColor Green
