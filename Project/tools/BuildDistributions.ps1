# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection — v0.6.1 Multi-Platform Packager

$ErrorActionPreference = "Stop"

$RepoRoot = (Get-Item -Path $PSScriptRoot).Parent.Parent.FullName
$StagingBase = Join-Path $RepoRoot "Build\staging_v061"
$DistOutDir = Join-Path $RepoRoot "Distribution\v0.6.1"
$DistSource = Join-Path $RepoRoot "Distribution\Uncharted"
$Staging051 = Join-Path $RepoRoot "Build\staging_v051\UNCHARTED - Sic Parvis Magna Collection"
$Staging060 = Join-Path $RepoRoot "Build\staging_v060"

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "UNCHARTED: Sic Parvis Magna Collection - Packager v0.6.1" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "Repository Root: $RepoRoot"
Write-Host "Output Directory: $DistOutDir"

# Ensure output directories exist
New-Item -ItemType Directory -Path $DistOutDir -Force | Out-Null
New-Item -ItemType Directory -Path $StagingBase -Force | Out-Null

# Copy README files from staging_v060 if needed
Get-ChildItem -Path $Staging060 -Filter "*.txt" | Copy-Item -Destination $StagingBase -Force

# ------------------------------------------------------------------------------
# STEP 1: Prepare Master Windows x64 Staging Directory
# ------------------------------------------------------------------------------
$Win64Master = Join-Path $StagingBase "master_win64\UNCHARTED-Sic-Parvis-Magna-Collection"
if (Test-Path $Win64Master) { Remove-Item $Win64Master -Recurse -Force }
New-Item -ItemType Directory -Path $Win64Master -Force | Out-Null

Write-Host "`n[1/5] Staging Master Windows x64 files..." -ForegroundColor Yellow
Copy-Item (Join-Path $DistSource "Uncharted - Sic Parvis Magna Collection.exe") $Win64Master -Force
Copy-Item (Join-Path $DistSource "Start Uncharted.exe") $Win64Master -Force
Copy-Item (Join-Path $DistSource "SPMCore.dll") $Win64Master -Force
Copy-Item (Join-Path $DistSource "SDL3.dll") $Win64Master -Force
Copy-Item (Join-Path $DistSource "libc++.dll") $Win64Master -Force
Copy-Item (Join-Path $DistSource "libunwind.dll") $Win64Master -Force
Copy-Item (Join-Path $DistSource "libwinpthread-1.dll") $Win64Master -Force
Copy-Item (Join-Path $DistSource "libiconv-2.dll") $Win64Master -Force

# Root Configs and Documentation
Copy-Item (Join-Path $RepoRoot "launcher.json") $Win64Master -Force
Copy-Item (Join-Path $DistSource "EngineConfig.ini") $Win64Master -Force
Set-Content -Path (Join-Path $Win64Master "VERSION") -Value "0.6.1" -Encoding UTF8 -NoNewline
Copy-Item (Join-Path $StagingBase "README.txt") $Win64Master -Force
Copy-Item (Join-Path $StagingBase "LEEME.txt") $Win64Master -Force

# Assets
$AssetsDst = Join-Path $Win64Master "assets"
New-Item -ItemType Directory -Path $AssetsDst -Force | Out-Null
Copy-Item (Join-Path $RepoRoot "Project\launcher\assets\*") $AssetsDst -Recurse -Force

# User Directory (clean, empty with .gitkeep)
$UserDst = Join-Path $Win64Master "user"
New-Item -ItemType Directory -Path $UserDst -Force | Out-Null
Set-Content -Path (Join-Path $UserDst ".gitkeep") -Value "" -Encoding UTF8 -NoNewline

# Core Directory
$CoreDst = Join-Path $Win64Master "core"
New-Item -ItemType Directory -Path $CoreDst -Force | Out-Null

if (Test-Path (Join-Path $Staging051 "core")) {
    Copy-Item (Join-Path $Staging051 "core\*") $CoreDst -Recurse -Force
} else {
    Copy-Item (Join-Path $DistSource "*") $CoreDst -Recurse -Force
}

Copy-Item (Join-Path $DistSource "SPMCore.dll") $CoreDst -Force
Copy-Item (Join-Path $DistSource "SDL3.dll") $CoreDst -Force
Copy-Item (Join-Path $DistSource "Start Uncharted.exe") $CoreDst -Force
Copy-Item (Join-Path $DistSource "EngineConfig.ini") $CoreDst -Force
Set-Content -Path (Join-Path $CoreDst "VERSION") -Value "0.6.1" -Encoding UTF8 -NoNewline

$PrepSrc = Join-Path $RepoRoot "Project\gow3_pc\scripts\prepare.py"
if (Test-Path $PrepSrc) {
    $CoreToolsDir = Join-Path $CoreDst "tools"
    New-Item -ItemType Directory -Path $CoreToolsDir -Force | Out-Null
    Copy-Item $PrepSrc $CoreToolsDir -Force
    $CoreScriptsDir = Join-Path $CoreDst "gow3_pc\scripts"
    New-Item -ItemType Directory -Path $CoreScriptsDir -Force | Out-Null
    Copy-Item $PrepSrc $CoreScriptsDir -Force
}

Get-ChildItem -Path $CoreDst -Include "*.obj","*.o","*.tmp","*.ninja*","CMakeLists.txt","CMakeCache.txt" -Recurse -ErrorAction SilentlyContinue | Remove-Item -Force
Write-Host "Master Windows x64 staging ready." -ForegroundColor Green

# ------------------------------------------------------------------------------
# STEP 2: Package Windows x64 Distributions (win11-x64, win10-x64, win7-x64)
# ------------------------------------------------------------------------------
$WindowsPlatforms = @("win11-x64", "win10-x64", "win7-x64")

foreach ($plat in $WindowsPlatforms) {
    Write-Host "`n[2/5] Packaging $plat..." -ForegroundColor Yellow
    $platStaging = Join-Path $StagingBase $plat
    if (Test-Path $platStaging) { Remove-Item $platStaging -Recurse -Force }
    New-Item -ItemType Directory -Path $platStaging -Force | Out-Null

    Copy-Item $Win64Master $platStaging -Recurse -Force
    $targetFolder = Join-Path $platStaging "UNCHARTED-Sic-Parvis-Magna-Collection"

    Set-Content -Path (Join-Path $targetFolder "PLATFORM") -Value $plat -Encoding UTF8 -NoNewline
    Set-Content -Path (Join-Path $targetFolder "core\PLATFORM") -Value $plat -Encoding UTF8 -NoNewline

    $zipName = "SPM-v0.6.1-beta-" + $plat + ".zip"
    $zipPath = Join-Path $DistOutDir $zipName
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

    Write-Host "Compressing $zipName..."
    Compress-Archive -Path (Join-Path $platStaging "*") -DestinationPath $zipPath -CompressionLevel Optimal
    $len = (Get-Item $zipPath).Length
    Write-Host ("Successfully packaged: " + $zipName + " (" + [string][math]::Round($len / 1048576, 2) + " MB)") -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# STEP 3: Package Windows x86 (32-bit Legacy Launcher / Tools)
# ------------------------------------------------------------------------------
Write-Host "`n[3/5] Packaging win-x86..." -ForegroundColor Yellow
$x86Staging = Join-Path $StagingBase "win-x86\UNCHARTED-Sic-Parvis-Magna-Collection"
if (Test-Path $x86Staging) { Remove-Item $x86Staging -Recurse -Force }
New-Item -ItemType Directory -Path $x86Staging -Force | Out-Null

$x86SpmDll = Join-Path $DistSource "x86\SPMCore.dll"
if (Test-Path $x86SpmDll) {
    Copy-Item $x86SpmDll $x86Staging -Force
}
Copy-Item (Join-Path $RepoRoot "launcher.json") $x86Staging -Force
Copy-Item (Join-Path $DistSource "EngineConfig.ini") $x86Staging -Force
Set-Content -Path (Join-Path $x86Staging "VERSION") -Value "0.6.1" -Encoding UTF8 -NoNewline
Set-Content -Path (Join-Path $x86Staging "PLATFORM") -Value "win-x86" -Encoding UTF8 -NoNewline

Copy-Item (Join-Path $StagingBase "README_x86.txt") (Join-Path $x86Staging "README.txt") -Force
Copy-Item (Join-Path $StagingBase "LEEME.txt") (Join-Path $x86Staging "LEEME.txt") -Force

Copy-Item (Join-Path $Win64Master "assets") $x86Staging -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $x86Staging "user") -Force | Out-Null
Set-Content -Path (Join-Path $x86Staging "user\.gitkeep") -Value "" -Encoding UTF8 -NoNewline

$x86ZipName = "SPM-v0.6.1-beta-win-x86.zip"
$x86ZipPath = Join-Path $DistOutDir $x86ZipName
if (Test-Path $x86ZipPath) { Remove-Item $x86ZipPath -Force }

Compress-Archive -Path (Join-Path $StagingBase "win-x86\*") -DestinationPath $x86ZipPath -CompressionLevel Optimal
$len86 = (Get-Item $x86ZipPath).Length
Write-Host ("Successfully packaged: " + $x86ZipName + " (" + [string][math]::Round($len86 / 1048576, 2) + " MB)") -ForegroundColor Green

# ------------------------------------------------------------------------------
# STEP 4: Package Linux x64 Native Runtime
# ------------------------------------------------------------------------------
Write-Host "`n[4/5] Packaging linux-x64..." -ForegroundColor Yellow
$linuxStaging = Join-Path $StagingBase "linux-x64\UNCHARTED-Sic-Parvis-Magna-Collection"
if (Test-Path $linuxStaging) { Remove-Item $linuxStaging -Recurse -Force }
New-Item -ItemType Directory -Path $linuxStaging -Force | Out-Null

$linuxInc = Join-Path $linuxStaging "include"
Copy-Item (Join-Path $RepoRoot "Project\spm_core\include") $linuxInc -Recurse -Force
$linuxSrc = Join-Path $linuxStaging "src"
Copy-Item (Join-Path $RepoRoot "Project\spm_core") $linuxSrc -Recurse -Force
Copy-Item (Join-Path $RepoRoot "Project\spm_launcher") $linuxStaging -Recurse -Force

$linuxTools = Join-Path $linuxStaging "tools"
New-Item -ItemType Directory -Path $linuxTools -Force | Out-Null
Copy-Item (Join-Path $RepoRoot "Project\tools\*.py") $linuxTools -Force
Copy-Item (Join-Path $RepoRoot "Project\gow3_pc\scripts\*.py") $linuxTools -Force

Copy-Item (Join-Path $RepoRoot "CMakeLists.txt") $linuxStaging -Force
Copy-Item (Join-Path $RepoRoot ".github\workflows\linux-build.yml") $linuxStaging -Force

Set-Content -Path (Join-Path $linuxStaging "VERSION") -Value "0.6.1" -Encoding UTF8 -NoNewline
Set-Content -Path (Join-Path $linuxStaging "PLATFORM") -Value "linux-x64" -Encoding UTF8 -NoNewline

Copy-Item (Join-Path $StagingBase "README_linux.txt") (Join-Path $linuxStaging "README.txt") -Force

$linuxZipName = "SPM-v0.6.1-beta-linux-x64.zip"
$linuxZipPath = Join-Path $DistOutDir $linuxZipName
if (Test-Path $linuxZipPath) { Remove-Item $linuxZipPath -Force }

Compress-Archive -Path (Join-Path $StagingBase "linux-x64\*") -DestinationPath $linuxZipPath -CompressionLevel Optimal
$lenLinux = (Get-Item $linuxZipPath).Length
Write-Host ("Successfully packaged: " + $linuxZipName + " (" + [string][math]::Round($lenLinux / 1048576, 2) + " MB)") -ForegroundColor Green

# ------------------------------------------------------------------------------
# STEP 5: Generate SHA-256 Checksums
# ------------------------------------------------------------------------------
Write-Host "`n[5/5] Generating SHA256SUMS.txt..." -ForegroundColor Yellow
$HashManifest = @()
Get-ChildItem -Path $DistOutDir -Filter "*.zip" | Sort-Object Name | ForEach-Object {
    $h = Get-FileHash -Path $_.FullName -Algorithm SHA256
    $entry = ($h.Hash.ToLowerInvariant() + "  " + $_.Name)
    $HashManifest += $entry
    Write-Host $entry
}

$ShaFile = Join-Path $DistOutDir "SHA256SUMS.txt"
$HashManifest | Set-Content -Path $ShaFile -Encoding UTF8

Write-Host "`nAll packages ready in $DistOutDir" -ForegroundColor Green
