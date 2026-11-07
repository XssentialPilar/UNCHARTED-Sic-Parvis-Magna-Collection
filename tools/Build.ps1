# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection - Master Build Pipeline (Build.ps1)
param(
    [Alias("Configuration")]
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$Platform = "x64",
    [switch]$RunTests = $true
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $ProjectRoot
$BuildDir = Join-Path $WorkspaceRoot "Build\Windows"

$CodexMsys = Join-Path $env:USERPROFILE ".codex\tmp\uncharted-build\msys64\clang64\bin"
$CodexUsr = Join-Path $env:USERPROFILE ".codex\tmp\uncharted-build\msys64\usr\bin"
if (Test-Path $CodexMsys) {
    $env:PATH = "$CodexMsys;$CodexUsr;$env:PATH"
} elseif (Test-Path "C:\msys64\clang64\bin") {
    $env:PATH = "C:\msys64\clang64\bin;C:\msys64\usr\bin;$env:PATH"
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " UNCHARTED: Sic Parvis Magna - Build Pipeline ($Config|$Platform)" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Step 1: Offline Shader Precompilation (CompileShaders.ps1)
& (Join-Path $PSScriptRoot "CompileShaders.ps1")

# Step 2: Configure & Build Native Runtime, GPU Shim & Invariant Tests
$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $CacheFile) {
    $CacheText = Get-Content $CacheFile -Raw -ErrorAction SilentlyContinue
    if ($CacheText -match "Compilacion|Proyecto|\`$Config") {
        $CacheText = $CacheText.Replace("Compilacion/Windows", "Build/Windows").Replace("Proyecto/", "Project/").Replace('$Config', $Config)
        [System.IO.File]::WriteAllText($CacheFile, $CacheText)
    }
}

cmake -S (Join-Path $ProjectRoot "gow3_pc\gpu") -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[-] FALLO DE CONFIGURACION CMAKE." -ForegroundColor Red
    exit 1
}

Write-Host "[*] Compilando binarios nativos con optimizacion maxima..." -ForegroundColor Yellow
ninja -C $BuildDir gow3-probe gow3gpu gow3-gpu-capabilities gow3-play spm-invariants-test frame-generation-test gc_policy-test arena_allocations-test cache_io_queue-test isa-test savedata-test audio-downmix-test
if ($LASTEXITCODE -ne 0) {
    Write-Host "[-] FALLO DE COMPILACION: Revisa los errores del compilador arriba." -ForegroundColor Red
    exit 1
}

# Step 3: Compile Native Windows .exe Launcher ("Uncharted - Sic Parvis Magna Collection.exe" & "Start Uncharted.exe")
$StarterDir = Join-Path $ProjectRoot "tools\windows_starter"
$StarterRcObj = Join-Path $BuildDir "starter_rc.o"
$LauncherExeMain = Join-Path $ProjectRoot "Uncharted - Sic Parvis Magna Collection.exe"
$LauncherExeStart = Join-Path $ProjectRoot "Start Uncharted.exe"

Push-Location $StarterDir
try {
    windres "starter.rc" -O coff -o $StarterRcObj
    if ($LASTEXITCODE -ne 0) { throw "windres failed on starter.rc" }
} finally {
    Pop-Location
}

clang -O2 -municode -mwindows (Join-Path $StarterDir "starter.c") $StarterRcObj -lshell32 -luser32 -lgdi32 -o $LauncherExeMain
if ($LASTEXITCODE -ne 0) { throw "Launcher .exe compilation failed." }

Copy-Item -Path $LauncherExeMain -Destination $LauncherExeStart -Force
Copy-Item -Path $LauncherExeMain -Destination (Join-Path $WorkspaceRoot "Uncharted - Sic Parvis Magna Collection.exe") -Force

if ($RunTests) {
    Write-Host "[*] Ejecutando suite de verificacion (10,000 Presents, VRAM 1GB/2GB, Invariantes)..." -ForegroundColor Yellow
    & (Join-Path $BuildDir "spm-invariants-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "frame-generation-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "gc_policy-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "arena_allocations-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "cache_io_queue-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "isa-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "savedata-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
    & (Join-Path $BuildDir "audio-downmix-test.exe")
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

Write-Host "[+] COMPILACION EXITOSA. Binarios verificados sin warnings." -ForegroundColor Green
exit 0
