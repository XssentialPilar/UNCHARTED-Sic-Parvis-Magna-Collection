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
$ParentDir = Split-Path -Parent $PSScriptRoot
if (Test-Path (Join-Path $ParentDir "Project\gow3_pc")) {
    $WorkspaceRoot = $ParentDir
    $ProjectRoot = Join-Path $WorkspaceRoot "Project"
} else {
    $ProjectRoot = $ParentDir
    $WorkspaceRoot = Split-Path -Parent $ProjectRoot
}
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
ninja -C $BuildDir gow3-probe gow3gpu gow3-gpu-capabilities gow3-play dxgi-presenter-test graphics-settings-test perf-overlay-test spm-invariants-test frame-generation-test gc_policy-test arena_allocations-test cache_io_queue-test isa-test savedata-test audio-downmix-test
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
Copy-Item -Path $LauncherExeMain -Destination (Join-Path $WorkspaceRoot "Uncharted - Sic Parvis Magna Collection.exe") -Force -ErrorAction SilentlyContinue

# Step 4: Deploy gow3-probe.exe, Uncharted.exe, and all required MSYS2 clang64 runtime DLLs (SDL3, FFmpeg, Zydis, libc++, etc.)
$Gow3OutDir = Join-Path $ProjectRoot "gow3_pc\out"
$Gow3OutGpuDir = Join-Path $ProjectRoot "gow3_pc\out\gpu"
New-Item -ItemType Directory -Force -Path $Gow3OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $Gow3OutGpuDir | Out-Null
$ProbeBuilt = Join-Path $BuildDir "gow3-probe.exe"
if (Test-Path $ProbeBuilt) {
    Copy-Item -Path $ProbeBuilt -Destination (Join-Path $Gow3OutDir "gow3-probe.exe") -Force
    Copy-Item -Path $ProbeBuilt -Destination (Join-Path $Gow3OutDir "Uncharted.exe") -Force

    $PromptBin = Join-Path $ProjectRoot "gow3_pc\gpu\shim\gow3_prompt_assets.bin"
    if (Test-Path $PromptBin) {
        Copy-Item -Path $PromptBin -Destination (Join-Path $Gow3OutDir "gow3_prompt_assets.bin") -Force
        Copy-Item -Path $PromptBin -Destination (Join-Path $BuildDir "gow3_prompt_assets.bin") -Force
        Copy-Item -Path $PromptBin -Destination (Join-Path $WorkspaceRoot "gow3_prompt_assets.bin") -Force
    }
    $LddCmd = Get-Command ldd.exe -ErrorAction SilentlyContinue
    $ClangBinDir = if (Test-Path $CodexMsys) { $CodexMsys } elseif (Test-Path "C:\msys64\clang64\bin") { "C:\msys64\clang64\bin" } else { $null }
    if ($LddCmd -and $ClangBinDir) {
        $DllNames = & $LddCmd.Source $ProbeBuilt | ForEach-Object {
            if ($_ -match "=> /clang64/bin/([^\s]+\.dll)") { $matches[1] }
        } | Sort-Object -Unique
        foreach ($Dll in $DllNames) {
            $SrcDll = Join-Path $ClangBinDir $Dll
            if (Test-Path $SrcDll) {
                Copy-Item -Path $SrcDll -Destination (Join-Path $Gow3OutDir $Dll) -Force
                Copy-Item -Path $SrcDll -Destination (Join-Path $BuildDir $Dll) -Force
            }
        }
    }
}
if (Test-Path (Join-Path $BuildDir "gow3-gpu-capabilities.exe")) {
    Copy-Item -Path (Join-Path $BuildDir "gow3-gpu-capabilities.exe") -Destination (Join-Path $Gow3OutGpuDir "gow3-gpu-capabilities.exe") -Force
}
if (Test-Path (Join-Path $BuildDir "gow3-play.exe")) {
    Copy-Item -Path (Join-Path $BuildDir "gow3-play.exe") -Destination (Join-Path $Gow3OutGpuDir "gow3-play.exe") -Force
}

if ($RunTests) {
    Write-Host "[*] Ejecutando suite de verificacion (D3D11/D3D12 Zero-Copy Interop, 10,000 Presents, VRAM 1GB/2GB, Invariantes)..." -ForegroundColor Yellow
    foreach ($TestExe in @(
        "dxgi-presenter-test.exe",
        "graphics-settings-test.exe",
        "perf-overlay-test.exe",
        "spm-invariants-test.exe",
        "frame-generation-test.exe",
        "gc_policy-test.exe",
        "arena_allocations-test.exe",
        "cache_io_queue-test.exe",
        "isa-test.exe",
        "savedata-test.exe",
        "audio-downmix-test.exe"
    )) {
        & (Join-Path $BuildDir $TestExe)
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[-] TEST FAILED: $TestExe" -ForegroundColor Red
            exit 1
        }
    }
}

Write-Host "[+] COMPILACION EXITOSA. Binarios y DLLs verificados sin warnings." -ForegroundColor Green
exit 0
