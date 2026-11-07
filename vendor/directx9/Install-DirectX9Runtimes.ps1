# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: Sic Parvis Magna Collection - Official DirectX End-User Runtimes (June 2010) Installer
param(
    [string]$DownloadDir = (Join-Path $PSScriptRoot "downloads"),
    [switch]$Force
)

$ErrorActionPreference = "Stop"

Write-Host "========================================================================" -ForegroundColor Cyan
Write-Host " Official DirectX End-User Runtimes (June 2010) Acquisition & Setup     " -ForegroundColor Cyan
Write-Host " Source: https://www.microsoft.com/download/details.aspx?id=8109        " -ForegroundColor Cyan
Write-Host "========================================================================" -ForegroundColor Cyan

$Url = "https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe"
$ExpectedSha256 = "439C277085C88B9A2BE741D440AD82E88A38A9BC6723B7AA9A4569E12DE7BC71"
$InstallerName = "directx_Jun2010_redist.exe"

if (-not (Test-Path $DownloadDir)) {
    New-Item -ItemType Directory -Path $DownloadDir -Force | Out-Null
}

$InstallerPath = Join-Path $DownloadDir $InstallerName

if ((Test-Path $InstallerPath) -and (-not $Force)) {
    Write-Host "[INFO] Package already downloaded: $InstallerPath" -ForegroundColor Yellow
} else {
    Write-Host "[1/3] Downloading official package from Microsoft..." -ForegroundColor Cyan
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $Url -OutFile $InstallerPath -UseBasicParsing
    Write-Host "[PASS] Download completed." -ForegroundColor Green
}

Write-Host "[2/3] Verifying SHA-256 cryptographic hash integrity..." -ForegroundColor Cyan
$actualHash = (Get-FileHash -Path $InstallerPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($actualHash -ne $ExpectedSha256) {
    Write-Host "[FAIL] SHA256 mismatch! Expected: $ExpectedSha256, Actual: $actualHash" -ForegroundColor Red
    throw "Corrupted or untrusted installer package detected."
}
Write-Host "[PASS] Integrity verified (SHA256: $actualHash)" -ForegroundColor Green

Write-Host "[3/3] Preparing extraction directory..." -ForegroundColor Cyan
$ExtractDir = Join-Path $DownloadDir "extracted"
if (-not (Test-Path $ExtractDir)) {
    New-Item -ItemType Directory -Path $ExtractDir -Force | Out-Null
}

Write-Host "[INFO] Extracting redistributable contents..." -ForegroundColor Cyan
Start-Process -FilePath $InstallerPath -ArgumentList "/Q /T:`"$ExtractDir`"" -Wait

$setupExe = Join-Path $ExtractDir "DXSETUP.exe"
if (Test-Path $setupExe) {
    Write-Host "[PASS] DXSETUP.exe extracted successfully." -ForegroundColor Green
    Write-Host "To execute installation, run: Start-Process -FilePath '$setupExe' -ArgumentList '/silent' -Verb RunAs" -ForegroundColor Cyan
} else {
    Write-Host "[WARN] Extraction completed. Check: $ExtractDir" -ForegroundColor Yellow
}

Write-Host "========================================================================" -ForegroundColor Cyan
