# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection - DirectX 9 Environment & Requirements Verifier
param(
    [switch]$Detailed
)

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " UNCHARTED: Sic Parvis Magna Collection - DirectX 9 Environment Check " -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

$allPass = $true

# 1. Check System d3d9.dll
$sysD3D9 = Join-Path $env:windir "System32\d3d9.dll"
if (Test-Path $sysD3D9) {
    $ver = (Get-Item $sysD3D9).VersionInfo.ProductVersion
    Write-Host "[PASS] Native Direct3D 9 Subsystem: Found at $sysD3D9 (Version: $ver)" -ForegroundColor Green
} else {
    Write-Host "[FAIL] Native Direct3D 9 Subsystem missing ($sysD3D9)" -ForegroundColor Red
    $allPass = $false
}

# 2. Check Direct3DCreate9Ex Entry Point
$csharpTest = @"
using System;
using System.Runtime.InteropServices;

public class D3D9Check {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LoadLibrary(string lpFileName);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    public static bool CheckDirect3DCreate9Ex() {
        IntPtr hMod = LoadLibrary("d3d9.dll");
        if (hMod == IntPtr.Zero) return false;
        IntPtr pProc = GetProcAddress(hMod, "Direct3DCreate9Ex");
        return pProc != IntPtr.Zero;
    }
}
"@
Add-Type -TypeDefinition $csharpTest -ErrorAction SilentlyContinue
if ([D3D9Check]::CheckDirect3DCreate9Ex()) {
    Write-Host "[PASS] Direct3D 9Ex Architecture: Direct3DCreate9Ex API entry point available (FlipEx supported)" -ForegroundColor Green
} else {
    Write-Host "[FAIL] Direct3D 9Ex entry point not found" -ForegroundColor Red
    $allPass = $false
}

# 3. Check D3D Compiler DLLs
$compilers = @("d3dcompiler_47.dll", "d3dcompiler_43.dll")
foreach ($comp in $compilers) {
    $path = Join-Path $env:windir "System32\$comp"
    if (Test-Path $path) {
        Write-Host "[PASS] Direct3D Shader Compiler ($comp): Available" -ForegroundColor Green
    } else {
        Write-Host "[WARN] $comp not in System32 (may use legacy redistributable)" -ForegroundColor Yellow
    }
}

# 4. Check D3DX9 (Legacy Extended Helpers)
$d3dx9 = Join-Path $env:windir "System32\d3dx9_43.dll"
if (Test-Path $d3dx9) {
    Write-Host "[PASS] DirectX 9 End-User Runtimes (d3dx9_43.dll): Installed" -ForegroundColor Green
} else {
    Write-Host "[INFO] d3dx9_43.dll not in System32. (Run Install-DirectX9Runtimes.ps1 to install optional June 2010 redistributable)" -ForegroundColor Cyan
}

Write-Host "----------------------------------------------------------------------" -ForegroundColor Gray
if ($allPass) {
    Write-Host "SUMMARY: Core DirectX 9 / Direct3D 9Ex environment is READY." -ForegroundColor Green
} else {
    Write-Host "SUMMARY: Some core DirectX 9 requirements are missing. Please inspect errors above." -ForegroundColor Red
}
Write-Host "======================================================================" -ForegroundColor Cyan
