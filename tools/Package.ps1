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

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$MainExe = Join-Path $ProjectRoot "Uncharted - Sic Parvis Magna Collection.exe"
$StartExe = Join-Path $ProjectRoot "Start Uncharted.exe"

if (Test-Path $MainExe) {
    Copy-Item -Path $MainExe -Destination (Join-Path $OutputDir "Uncharted - Sic Parvis Magna Collection.exe") -Force
    Copy-Item -Path $MainExe -Destination (Join-Path $WorkspaceRoot "Uncharted - Sic Parvis Magna Collection.exe") -Force
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
New-Item -ItemType Directory -Force -Path $Gow3OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $Gow3OutGpuDir | Out-Null
if (Test-Path $ProbeExe) {
    Copy-Item -Path $ProbeExe -Destination (Join-Path $OutputDir "gow3-probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $OutputDir "uncharted_probe.exe") -Force
    Copy-Item -Path $ProbeExe -Destination (Join-Path $Gow3OutDir "gow3-probe.exe") -Force
}
if (Test-Path $GpuDll) {
    Copy-Item -Path $GpuDll -Destination (Join-Path $OutputDir "libgow3gpu.dll") -Force
    Copy-Item -Path $GpuDll -Destination (Join-Path $Gow3OutDir "libgow3gpu.dll") -Force
}
if (Test-Path $GpuCapExe) {
    Copy-Item -Path $GpuCapExe -Destination (Join-Path $OutputDir "gow3-gpu-capabilities.exe") -Force
    Copy-Item -Path $GpuCapExe -Destination (Join-Path $Gow3OutGpuDir "gow3-gpu-capabilities.exe") -Force
}
if (Test-Path $PlayExe) {
    Copy-Item -Path $PlayExe -Destination (Join-Path $OutputDir "gow3-play.exe") -Force
    Copy-Item -Path $PlayExe -Destination (Join-Path $Gow3OutGpuDir "gow3-play.exe") -Force
}

Copy-Item -Path (Join-Path $ProjectRoot "launcher") -Destination $OutputDir -Recurse -Force
$ProbePy = Join-Path $ProjectRoot "tools\probe_uncharted.py"
if (Test-Path $ProbePy) {
    New-Item -ItemType Directory -Force -Path (Join-Path $OutputDir "tools") | Out-Null
    Copy-Item -Path $ProbePy -Destination (Join-Path $OutputDir "tools\probe_uncharted.py") -Force
}

# Generate SHA-256 integrity manifest (Section 6.1 requirement)
$HashLines = @()
Get-ChildItem -Path $OutputDir -File | Sort-Object Name | ForEach-Object {
    $h = Get-FileHash -Path $_.FullName -Algorithm SHA256
    $HashLines += "$($h.Hash.ToLowerInvariant())  $($_.Name)"
}
$ShaFile = Join-Path $OutputDir "SHA256SUMS.txt"
$HashLines | Set-Content -Path $ShaFile -Encoding UTF8

Write-Host "[Package] Packaged Uncharted: Sic Parvis Magna Collection and SHA256SUMS.txt into: $OutputDir" -ForegroundColor Green
