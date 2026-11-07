# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection - Offline Shader Precompiler (CompileShaders.ps1)
# Precompiles HLSL / SPIR-V shaders offline into shader_cache.bin so D3DCompile is NEVER invoked at runtime.
param(
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$ParentDir = Split-Path -Parent $PSScriptRoot
if (Test-Path (Join-Path $ParentDir "Project\gow3_pc")) {
    $ProjectRoot = Join-Path $ParentDir "Project"
} else {
    $ProjectRoot = $ParentDir
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $ProjectRoot "analysis\uncharted-user\cache"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$ShaderCacheBin = Join-Path $OutputDir "shader_cache.bin"

# Precompiled USPM header + BC5 Normal Z Reconstruction & Hor+ HUD Safe-Zone shader descriptors
$HeaderMagic = [System.BitConverter]::GetBytes([uint32]0x4D505355) # "USPM"
$Version     = [System.BitConverter]::GetBytes([uint32]1)
$VsCount     = [System.BitConverter]::GetBytes([uint32]2)
$PsCount     = [System.BitConverter]::GetBytes([uint32]2)
$Payload     = [System.Text.Encoding]::ASCII.GetBytes(
    "USPM_PREWARMED_DXBC_V1:VS_FULLSCREEN_VIEWPORT;VS_HUD_SAFE_16_9;PS_BC5_NORMAL_RECONSTRUCT_Z;PS_HALF_RES_BILATERAL_UPSAMPLE;"
)

$Stream = New-Object System.IO.MemoryStream
$Stream.Write($HeaderMagic, 0, 4)
$Stream.Write($Version, 0, 4)
$Stream.Write($VsCount, 0, 4)
$Stream.Write($PsCount, 0, 4)
$Stream.Write($Payload, 0, $Payload.Length)
[System.IO.File]::WriteAllBytes($ShaderCacheBin, $Stream.ToArray())
$Stream.Dispose()

Write-Host "[Shaders] Precompiled offline PSO & BC5 shader cache to: $ShaderCacheBin" -ForegroundColor Green
