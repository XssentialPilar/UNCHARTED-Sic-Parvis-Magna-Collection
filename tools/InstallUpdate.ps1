# SPDX-License-Identifier: GPL-2.0-or-later
# In-Place Recoverable Updater for UNCHARTED: Sic Parvis Magna Collection
param(
    [Parameter(Mandatory=$true)]
    [string]$StagedDir,
    [Parameter(Mandatory=$true)]
    [int]$WaitPid,
    [string]$TargetExe = "Uncharted - Sic Parvis Magna Collection.exe"
)

$ErrorActionPreference = "Stop"

# Copies a file, or the *contents* of a directory into an existing one. Passing a
# directory to Copy-Item -Destination when that directory already exists nests it
# (core\core, tools\tools), which used to break every in-place update.
function Copy-Tree {
    param([string]$Source, [string]$Destination)
    if (Test-Path -LiteralPath $Source -PathType Container) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        foreach ($child in Get-ChildItem -LiteralPath $Source -Force) {
            Copy-Tree -Source $child.FullName -Destination (Join-Path $Destination $child.Name)
        }
    } else {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

Write-Host "=========================================================="
Write-Host "UNCHARTED: Sic Parvis Magna Collection - In-Place Updater"
Write-Host "=========================================================="

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Write-Host "[1/5] Waiting for launcher (PID $WaitPid) to exit cleanly..."
$timeoutSeconds = 30
$timer = [System.Diagnostics.Stopwatch]::StartNew()
while ($timer.Elapsed.TotalSeconds -lt $timeoutSeconds) {
    $proc = Get-Process -Id $WaitPid -ErrorAction SilentlyContinue
    if (-not $proc) {
        break
    }
    Start-Sleep -Milliseconds 500
}
$timer.Stop()

# Safety wait for file handles to unlock
Start-Sleep -Seconds 1

Write-Host "[2/5] Staging replacement from: $StagedDir"
if (-not (Test-Path $StagedDir)) {
    Write-Error "Staged update directory not found: $StagedDir"
    exit 1
}

$ProtectedItems = @("user", "analysis", "update", "launcher.json", "Logs", "user_saves")

$BackupDir = Join-Path $RootDir "update\backup"
if (Test-Path $BackupDir) {
    Remove-Item -Path $BackupDir -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null

try {
    Write-Host "[3/5] Applying update over root while protecting user data..."
    $stagedItems = Get-ChildItem -Path $StagedDir
    if ($stagedItems.Count -eq 1 -and $stagedItems[0].PSIsContainer) {
        Write-Host "   -> Unwrapping root container folder: $($stagedItems[0].Name)"
        $stagedItems = Get-ChildItem -Path $stagedItems[0].FullName
    }
    foreach ($item in $stagedItems) {
        if ($ProtectedItems -contains $item.Name) {
            Write-Host "   -> Preserving user-protected item: $($item.Name)"
            continue
        }

        $destPath = Join-Path $RootDir $item.Name
        if (Test-Path $destPath) {
            # Backup first for recovery
            $backupItem = Join-Path $BackupDir $item.Name
            Copy-Tree -Source $destPath -Destination $backupItem
        }

        # Copy new item over root (merging folders, never nesting them)
        Copy-Tree -Source $item.FullName -Destination $destPath
        Write-Host "   -> Updated: $($item.Name)"
    }

    Write-Host "[4/5] Cleaning up staging directory..."
    Remove-Item -Path $StagedDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path $BackupDir -Recurse -Force -ErrorAction SilentlyContinue

    Write-Host "[5/5] Relaunching updated application..."
    $exePath = Join-Path $RootDir $TargetExe
    if (Test-Path $exePath) {
        Start-Process -FilePath $exePath
        Write-Host "Launch successful! Update complete."
    } else {
        Write-Host "Warning: $exePath not found, checking fallback executables..."
        $fallback = Get-ChildItem -Path $RootDir -Filter "*.exe" | Select-Object -First 1
        if ($fallback) {
            Start-Process -FilePath $fallback.FullName
        }
    }
}
catch {
    Write-Warning "Update error encountered: $_"
    Write-Host "Attempting rollback from backup..."
    if (Test-Path $BackupDir) {
        $backupItems = Get-ChildItem -Path $BackupDir
        foreach ($b in $backupItems) {
            Copy-Tree -Source $b.FullName -Destination (Join-Path $RootDir $b.Name)
        }
        Write-Host "Rollback completed. Original installation preserved."
    }
    exit 1
}
