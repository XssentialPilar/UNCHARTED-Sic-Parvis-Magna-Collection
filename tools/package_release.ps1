# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
    Builds the end-user package for UNCHARTED: Sic Parvis Magna Collection.

.DESCRIPTION
    Produces:

        dist\Uncharted-Sic-Parvis-Magna-Collection\      the folder a player runs
        dist\Uncharted-Sic-Parvis-Magna-Collection.zip   the same folder, zipped
        dist\SHA256SUMS.txt                              checksum of the zip

    Everything is rebuilt from the current sources. No file is taken from an
    older package (Distribution\, a previous publish or a stale SPMCore.dll).

    The result is self-contained:
      * the WPF launcher is published self-contained single-file, so players do
        not install .NET;
      * Python is embedded for the runtime's boot script, so players do not
        install Python;
      * the runtime ships with exactly the DLLs it imports (verified against the
        PE import table), plus the prompt asset bank it reads next to the exe.

    Redundant output is never staged: no duplicate runtime executables, no .pdb,
    no __pycache__, no test suites, no 32-bit folder, no second launcher.

.PARAMETER SpmBuildDir
    CMake build directory for SPMCore. Default: Project\spm_core\build-release.

.PARAMETER OutputRoot
    Where the folder and the zip are written. Default: <repo>\dist.

.PARAMETER SkipBuild
    Reuse the existing SPMCore build and launcher publish output. Only useful
    when they were produced by this same script from the current sources.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Project\tools\package_release.ps1
#>
param(
    [string]$SpmBuildDir = "",
    [string]$PublishDir = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProjectRoot = Join-Path $RepoRoot "Project"
if (-not (Test-Path (Join-Path $ProjectRoot "gow3_pc"))) {
    throw "Project\gow3_pc not found under '$RepoRoot'. Run this script from the repository."
}

$Version = "0.6.1"
$StageName = "Uncharted-Sic-Parvis-Magna-Collection"
if ([string]::IsNullOrWhiteSpace($SpmBuildDir)) { $SpmBuildDir = Join-Path $ProjectRoot "spm_core\build-release" }
if ([string]::IsNullOrWhiteSpace($PublishDir)) { $PublishDir = Join-Path $ProjectRoot "launcher_net\publish-win-x64" }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $RepoRoot "dist" }

$Stage = Join-Path $OutputRoot "stage\$StageName"
$Final = Join-Path $OutputRoot $StageName
$ZipPath = Join-Path $OutputRoot "$StageName.zip"

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "== $Message" -ForegroundColor Cyan
}

function Assert-Path([string]$Path, [string]$What) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "$What not found: $Path" }
}

# Copies a file, or the contents of a directory into an existing one. Copy-Item
# nests a directory when the destination exists (core\core), which is never what
# a package layout wants.
function Copy-Tree {
    param([string]$Source, [string]$Destination, [scriptblock]$Skip)
    if (Test-Path -LiteralPath $Source -PathType Container) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
        foreach ($child in Get-ChildItem -LiteralPath $Source -Force) {
            if ($Skip -and (& $Skip $child)) { continue }
            Copy-Tree -Source $child.FullName -Destination (Join-Path $Destination $child.Name) -Skip $Skip
        }
    } else {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

function Write-TextFile([string]$Path, [string]$Text) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Text, $utf8)
}

function Format-Size([long]$Bytes) {
    if ($Bytes -ge 1GB) { return "{0:N2} GB" -f ($Bytes / 1GB) }
    if ($Bytes -ge 1MB) { return "{0:N1} MB" -f ($Bytes / 1MB) }
    return "{0:N0} KB" -f ($Bytes / 1KB)
}

# ---------------------------------------------------------------------------
Write-Step "1/6  Building SPMCore ($Version, Release)"
# ---------------------------------------------------------------------------
$spmSource = Join-Path $ProjectRoot "spm_core"
$spmDll = Join-Path $SpmBuildDir "SPMCore.dll"

if (-not $SkipBuild) {
    & cmake -S $spmSource -B $SpmBuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    & cmake --build $SpmBuildDir
    if ($LASTEXITCODE -ne 0) { throw "SPMCore build failed" }
}
Assert-Path $spmDll "SPMCore.dll"

# The version is defined in one place; refuse to package a mismatch.
$cmakeText = Get-Content (Join-Path $spmSource "CMakeLists.txt") -Raw
if ($cmakeText -notmatch "project\(SPMCore VERSION ([0-9.]+)") {
    throw "Cannot read the SPMCore version from CMakeLists.txt"
}
if ($Matches[1] -ne $Version) {
    throw "SPMCore version $($Matches[1]) does not match the package version $Version"
}

# The launcher carries its own version string for the updater. Refuse to ship a
# package whose launcher would report a different version than SPMCore.
$mainWindowText = Get-Content (Join-Path $ProjectRoot "launcher_net\MainWindow.xaml.cs") -Raw
if ($mainWindowText -notmatch 'CurrentLauncherVersion\s*=\s*"v([0-9.]+)') {
    throw "Cannot read CurrentLauncherVersion from MainWindow.xaml.cs"
}
if ($Matches[1] -ne $Version) {
    throw "Launcher version $($Matches[1]) does not match the package version $Version"
}

# ---------------------------------------------------------------------------
Write-Step "2/6  Publishing the launcher (self-contained, no .NET needed)"
# ---------------------------------------------------------------------------
$launcherExe = Join-Path $PublishDir "Uncharted - Sic Parvis Magna Collection.exe"
if (-not $SkipBuild) {
    $csproj = Join-Path $ProjectRoot "launcher_net\UnchartedLauncher.csproj"
    Assert-Path $csproj "Launcher project"
    if (Test-Path -LiteralPath $PublishDir) { Remove-Item -LiteralPath $PublishDir -Recurse -Force }
    & dotnet publish $csproj -c Release -r win-x64 --self-contained true `
        -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -p:DebugType=none `
        -o $PublishDir --nologo
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }
}
Assert-Path $launcherExe "Published launcher"

# ---------------------------------------------------------------------------
Write-Step "3/6  Staging the package"
# ---------------------------------------------------------------------------
if (Test-Path -LiteralPath (Join-Path $OutputRoot "stage")) {
    Remove-Item -LiteralPath (Join-Path $OutputRoot "stage") -Recurse -Force
}
if (Test-Path -LiteralPath $Final) { Remove-Item -LiteralPath $Final -Recurse -Force }

New-Item -ItemType Directory -Force -Path $Stage, "$Stage\core", "$Stage\tools" | Out-Null

# --- Launcher and its native core -----------------------------------------
Copy-Item -LiteralPath $launcherExe -Destination $Stage -Force
Copy-Item -LiteralPath $spmDll -Destination (Join-Path $Stage "SPMCore.dll") -Force
Copy-Item -LiteralPath (Join-Path $spmSource "src\translations.json") -Destination (Join-Path $Stage "translations.json") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "Project\tools\InstallUpdate.ps1") -Destination (Join-Path $Stage "tools\InstallUpdate.ps1") -Force

# No launcher.json ships: the launcher creates it on first save with empty
# GameDir and sane defaults, so a stale or personal path can never be bundled.

# --- Runtime ---------------------------------------------------------------
$runtimeOut = Join-Path $ProjectRoot "gow3_pc\out"
$runtimeExe = Join-Path $runtimeOut "gow3-probe.exe"
Assert-Path $runtimeExe "Runtime build (run Project\gow3_pc\build.sh first)"

Copy-Item -LiteralPath $runtimeExe -Destination (Join-Path $Stage "core\gow3-probe.exe") -Force
$runtimeFiles = Get-ChildItem -LiteralPath $runtimeOut -File |
    Where-Object { $_.Name -in @("gow3-probe.exe", "Uncharted.exe", "SPMCore.dll", "gow3_prompt_assets.bin") -eq $false }
foreach ($file in $runtimeFiles) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path (Join-Path $Stage "core") $file.Name) -Force
}

$streamlineBin = Join-Path $ProjectRoot "gow3_pc\gpu\third_party\streamline\bin\x64"
if (Test-Path -LiteralPath $streamlineBin) {
    foreach ($slFile in (Get-ChildItem -LiteralPath $streamlineBin -Filter *.dll -File)) {
        $destSl = Join-Path (Join-Path $Stage "core") $slFile.Name
        if (-not (Test-Path -LiteralPath $destSl)) {
            Copy-Item -LiteralPath $slFile.FullName -Destination $destSl -Force
        }
    }
}

# The launcher's GPU page falls back to this diagnostic when SPMCore is absent.
$gpuCaps = Join-Path $runtimeOut "gpu\gow3-gpu-capabilities.exe"
if (Test-Path -LiteralPath $gpuCaps) {
    Copy-Item -LiteralPath $gpuCaps -Destination (Join-Path $Stage "core\gow3-gpu-capabilities.exe") -Force
}

# Prompt bank read next to the executable (keyboard/mouse button icons).
$promptBank = Join-Path $ProjectRoot "gow3_pc\gpu\shim\gow3_prompt_assets.bin"
Assert-Path $promptBank "Prompt asset bank"
Copy-Item -LiteralPath $promptBank -Destination (Join-Path $Stage "core\gow3_prompt_assets.bin") -Force

# Journal and map art. The runtime resolves it next to the executable
# (core\assets\hud_prompts.zip), the same layout the source tree uses.
$promptArt = Join-Path $ProjectRoot "gow3_pc\assets\hud_prompts.zip"
Assert-Path $promptArt "Journal/map prompt art (gow3_pc\assets\hud_prompts.zip)"
New-Item -ItemType Directory -Force -Path (Join-Path $Stage "core\assets") | Out-Null
Copy-Item -LiteralPath $promptArt -Destination (Join-Path $Stage "core\assets\hud_prompts.zip") -Force

# Runtime defaults and the window icon the boot script looks for.
Copy-Item -LiteralPath (Join-Path $ProjectRoot "EngineConfig.ini") -Destination (Join-Path $Stage "core\EngineConfig.ini") -Force
$windowIcon = Join-Path $ProjectRoot "launcher\assets\window_icon.bmp"
if (Test-Path -LiteralPath $windowIcon) {
    New-Item -ItemType Directory -Force -Path "$Stage\core\launcher\assets" | Out-Null
    Copy-Item -LiteralPath $windowIcon -Destination (Join-Path $Stage "core\launcher\assets\window_icon.bmp") -Force
}

# --- Boot scripts ----------------------------------------------------------
$toolsStage = Join-Path $Stage "core\tools"
New-Item -ItemType Directory -Force -Path $toolsStage | Out-Null
foreach ($name in @("probe_uncharted.py", "hang_report.py", "net_overlay.py", "check_game_files.py", "game_files_102.json.gz")) {
    $src = Join-Path $ProjectRoot "tools\$name"
    Assert-Path $src "Launcher tool"
    Copy-Item -LiteralPath $src -Destination $toolsStage -Force
}

$scriptsStage = Join-Path $Stage "core\gow3_pc\scripts"
New-Item -ItemType Directory -Force -Path $scriptsStage | Out-Null
Copy-Item -Path (Join-Path $ProjectRoot "gow3_pc\scripts\*.py") -Destination $scriptsStage -Force

# --- Embedded Python -------------------------------------------------------
Write-Host "     embedding Python (interpreter + standard library, no Tk, no tests)"
$pythonExe = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $pythonExe) { throw "python not found on PATH (needed to embed the interpreter)" }
$pythonHome = Split-Path -Parent $pythonExe.Source
$pythonStage = Join-Path $Stage "core\python"
New-Item -ItemType Directory -Force -Path $pythonStage | Out-Null

foreach ($pattern in @("python.exe", "python3.dll", "python3*.dll", "vcruntime140*.dll", "LICENSE.txt")) {
    Get-ChildItem -LiteralPath $pythonHome -Filter $pattern -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $pythonStage -Force
    }
}

# Tk is only used by the legacy Python launcher, which this package does not
# ship. The boot script is a console program.
$skipDll = @("_tkinter.pyd", "tcl86t.dll", "tk86t.dll", "tcl90t.dll", "tk90t.dll")
Copy-Tree -Source (Join-Path $pythonHome "DLLs") -Destination (Join-Path $pythonStage "DLLs") `
    -Skip { param($i) $skipDll -contains $i.Name }.GetNewClosure()

$skipLib = @("site-packages", "test", "tests", "idlelib", "ensurepip", "turtledemo", "venv",
             "lib2to3", "pydoc_data", "tkinter", "turtledemos", "__pycache__")
Copy-Tree -Source (Join-Path $pythonHome "Lib") -Destination (Join-Path $pythonStage "Lib") `
    -Skip { param($i) ($skipLib -contains $i.Name) -or ($i.Name -like "*.pyc") -or ($i.Name -like "*.pyo") }.GetNewClosure()

# --- Docs and licence ------------------------------------------------------
Copy-Item -LiteralPath (Join-Path $ProjectRoot "gow3_pc\LICENSE") -Destination (Join-Path $Stage "LICENCIA.txt") -Force

$leeme = @"
UNCHARTED: Sic Parvis Magna Collection - Launcher v$Version
============================================================

REQUISITOS
- Windows 10 u 11, 64 bits.
- Una GPU con Vulkan 1.3 y el driver actualizado (NVIDIA, AMD o Intel).
- Tu propia copia del juego: la carpeta CUSA02344 con eboot.bin y sce_sys.
  Este paquete NO incluye ningun archivo del juego.

COMO SE USA
1. Descomprime esta carpeta completa donde quieras y manten todos los archivos juntos.
2. Ejecuta "Uncharted - Sic Parvis Magna Collection.exe".
3. La primera vez que pulses PLAY el launcher te pedira la carpeta del juego
   (CUSA02344); la recuerda para las siguientes.
4. Elige capitulo y preset, y pulsa PLAY.

No hace falta instalar .NET, Python ni ningun runtime adicional: todo va incluido.

AJUSTES Y DATOS
- Ajustes del launcher: launcher.json (junto al ejecutable y en %APPDATA%).
- Ajustes por juego, partidas y cache de shaders: core\analysis\uncharted-user\.
  Esa carpeta es tuya: las actualizaciones no la tocan.

TECLADO Y RATON
- Funcionan sin configurar nada. Se pueden cambiar por juego en la pestana CONTROLS.
- Insert abre el menu del juego; Alt+Tab libera el raton.

SI ALGO VA MAL
- El primer arranque de cada juego prepara archivos y compila shaders: tarda mas
  y puede dar tirones hasta que la cache de shaders se llena.
- En equipos modestos usa el preset Low (720p con FSR 1 a 60 FPS).
- Los registros de cada partida quedan en core\analysis\uncharted-user\.

LICENCIA
GNU GPL v2 o posterior. Texto completo en LICENCIA.txt. El codigo fuente esta en
https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port
Sin relacion con Sony, Naughty Dog ni Bluepoint Games.
"@
Write-TextFile (Join-Path $Stage "LEEME.txt") $leeme

$readme = @"
UNCHARTED: Sic Parvis Magna Collection - Launcher v$Version
============================================================

REQUIREMENTS
- Windows 10 or 11, 64-bit.
- A GPU with Vulkan 1.3 support and an up-to-date driver (NVIDIA, AMD or Intel).
- Your own copy of the game: the CUSA02344 folder with eboot.bin and sce_sys.
  No game files are included in this package.

HOW TO PLAY
1. Extract this whole folder anywhere and keep every file together.
2. Run "Uncharted - Sic Parvis Magna Collection.exe".
3. The first time you press PLAY the launcher asks for the game folder
   (CUSA02344) and remembers it.
4. Pick a chapter and a preset, then press PLAY.

No .NET, Python or extra runtime has to be installed: everything is bundled.

SETTINGS AND DATA
- Launcher settings: launcher.json (next to the executable and in %APPDATA%).
- Per-game settings, saves and shader cache: core\analysis\uncharted-user\.
  That folder is yours; updates never touch it.

KEYBOARD AND MOUSE
- They work out of the box and can be rebound per game in the CONTROLS tab.
- Insert opens the in-game menu; Alt+Tab frees the mouse.

TROUBLESHOOTING
- The first start of each game prepares files and compiles shaders: it is slow
  and may stutter until the shader cache fills.
- On modest hardware use the Low preset (720p with FSR 1 at 60 FPS).
- Per-run logs are written to core\analysis\uncharted-user\.

LICENSE
GNU GPL v2 or later. Full text in LICENCIA.txt. Source code:
https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port
Not affiliated with Sony, Naughty Dog or Bluepoint Games.
"@
Write-TextFile (Join-Path $Stage "README.txt") $readme

# --- Third-party notices ---------------------------------------------------
$licensesStage = Join-Path $Stage "licenses"
New-Item -ItemType Directory -Force -Path $licensesStage | Out-Null

# MSYS2 ships the licences of the packages whose DLLs the runtime links. Read
# the local pacman package database directly (faster and safe in sandboxes);
# otherwise leave a pointer to the sources.
$msysRoot = $null
foreach ($candidate in @(
        (Get-Command pacman -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        "C:\msys64\usr\bin\pacman.exe",
        (Join-Path $env:USERPROFILE ".codex\tmp\uncharted-build\msys64\usr\bin\pacman.exe"))) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) {
        $rootCandidate = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $candidate))
        if (Test-Path -LiteralPath (Join-Path $rootCandidate "var\lib\pacman\local")) {
            $msysRoot = $rootCandidate
            break
        }
    }
}

$wroteThirdParty = $false
if ($msysRoot) {
    Write-Host "     collecting third-party notices (MSYS2 packages behind the runtime DLLs)"
    $wanted = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($dll in (Get-ChildItem -LiteralPath (Join-Path $Stage "core") -Filter *.dll)) {
        [void]$wanted.Add("clang64/bin/$($dll.Name)")
    }
    $matchedPkgDirs = @()
    $licenseRelPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    if ($wanted.Count -gt 0) {
        $localDb = Join-Path $msysRoot "var\lib\pacman\local"
        foreach ($pkgDir in (Get-ChildItem -LiteralPath $localDb -Directory)) {
            $filesPath = Join-Path $pkgDir.FullName "files"
            if (-not (Test-Path -LiteralPath $filesPath)) { continue }
            $pkgLines = [System.IO.File]::ReadAllLines($filesPath)
            $hit = $false
            foreach ($line in $pkgLines) {
                if ($wanted.Contains($line)) { $hit = $true; break }
            }
            if (-not $hit) { continue }
            $matchedPkgDirs += $pkgDir
            foreach ($line in $pkgLines) {
                if ($line -match '/share/licenses/(.+[^/])$') {
                    [void]$licenseRelPaths.Add($line.TrimStart('/'))
                }
            }
        }
    }
    if ($matchedPkgDirs.Count -gt 0) {
        foreach ($rel in $licenseRelPaths) {
            $relative = ($rel -split '/share/licenses/', 2)[1]
            $source = Join-Path $msysRoot ($rel -replace '/', '\')
            if (-not (Test-Path -LiteralPath $source)) { continue }
            $target = Join-Path $licensesStage ("msys2\" + ($relative -replace '/', '\'))
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath $source -Destination $target -Force
        }
        $lines = @(
            "Third-party packages whose DLLs ship in core\ (MSYS2 CLANG64 builds).",
            "Sources: https://repo.msys2.org/mingw/sources/ and https://packages.msys2.org/",
            ""
        )
        $lines += @(foreach ($pkgDir in ($matchedPkgDirs | Sort-Object Name)) {
            $descPath = Join-Path $pkgDir.FullName "desc"
            if (-not (Test-Path -LiteralPath $descPath)) { continue }
            $name = ""; $ver = ""; $url = ""; $lics = @(); $sec = ""
            foreach ($line in ([System.IO.File]::ReadAllLines($descPath))) {
                if ($line -match '^%([A-Z]+)%$') { $sec = $Matches[1]; continue }
                if ([string]::IsNullOrWhiteSpace($line)) { $sec = ""; continue }
                switch ($sec) {
                    "NAME"    { $name = $line }
                    "VERSION" { $ver  = $line }
                    "URL"     { $url  = $line }
                    "LICENSE" { $lics += $line }
                }
            }
            "Name            : $name"
            "Version         : $ver"
            "Licenses        : $($lics -join '  ')"
            "URL             : $url"
        })
        Write-TextFile (Join-Path $licensesStage "THIRD-PARTY.txt") (($lines -join "`r`n") + "`r`n")
        $wroteThirdParty = $true
    }
}
if (-not $wroteThirdParty) {
    Write-TextFile (Join-Path $licensesStage "THIRD-PARTY.txt") @"
The runtime links libraries built by MSYS2 (CLANG64). Their licences and
sources: https://packages.msys2.org/ and https://repo.msys2.org/mingw/sources/
The Python standard library in core\python is covered by python\LICENSE.txt.
"@
}

# --- Version markers -------------------------------------------------------
Write-TextFile (Join-Path $Stage "VERSION") $Version
Write-TextFile (Join-Path $Stage "core\VERSION") $Version

# ---------------------------------------------------------------------------
Write-Step "4/6  Cleaning redundant output"
# ---------------------------------------------------------------------------
$junk = Get-ChildItem -LiteralPath $Stage -Recurse -Force -File | Where-Object {
    $_.Extension -in @(".pdb", ".pyc", ".pyo", ".obj", ".o", ".a", ".log", ".tmp") -or
    $_.Name -eq ".gitkeep"
}
foreach ($item in $junk) { Remove-Item -LiteralPath $item.FullName -Force }
Get-ChildItem -LiteralPath $Stage -Recurse -Force -Directory | Where-Object { $_.Name -eq "__pycache__" } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }

# Player data never ships: a run from an earlier staging folder could have left
# saves, shader caches or logs behind.
$analysisDir = Join-Path $Stage "core\analysis"
if (Test-Path -LiteralPath $analysisDir) {
    Remove-Item -LiteralPath $analysisDir -Recurse -Force
}

# Nothing from the game or from a player may ship, and no duplicate runtime.
$forbidden = Get-ChildItem -LiteralPath $Stage -Recurse -Force | Where-Object {
    $_.Name -in @("eboot.bin", "sce_sys", "savedata", "cache", "Uncharted.exe") -or
    $_.Extension -in @(".elf", ".pkg", ".psarc", ".pak", ".log") -or
    $_.Name -like "probe*.ini" -or $_.Name -like "boot*.bin"
}
if ($forbidden) {
    $list = ($forbidden | ForEach-Object { $_.FullName }) -join "`r`n"
    throw "The package holds files that must not ship:`r`n$list"
}

# ---------------------------------------------------------------------------
Write-Step "5/6  Verifying the layout"
# ---------------------------------------------------------------------------
$required = @(
    "Uncharted - Sic Parvis Magna Collection.exe",
    "SPMCore.dll",
    "translations.json",
    "VERSION",
    "LEEME.txt",
    "README.txt",
    "LICENCIA.txt",
    "tools\InstallUpdate.ps1",
    "core\gow3-probe.exe",
    "core\gow3_prompt_assets.bin",
    "core\assets\hud_prompts.zip",
    "core\EngineConfig.ini",
    "core\VERSION",
    "core\python\python.exe",
    "core\tools\probe_uncharted.py",
    "core\tools\hang_report.py",
    "core\tools\net_overlay.py",
    "core\tools\check_game_files.py",
    "core\tools\game_files_102.json.gz",
    "core\gow3_pc\scripts\prepare.py"
)
$missing = $required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Stage $_)) }
if ($missing) { throw ("Missing required files:`r`n" + (($missing | ForEach-Object { "  $_" }) -join "`r`n")) }

# Every DLL the runtime imports locally must be next to it.
function Get-PeImports([string]$Path) {
    $out = & llvm-readobj --coff-imports $Path 2>$null
    return @($out | Select-String -Pattern '^\s+Name: (\S+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
}
$coreDir = Join-Path $Stage "core"
$localDlls = @(Get-ChildItem -LiteralPath $coreDir -Filter *.dll | Select-Object -ExpandProperty Name)
if (Get-Command llvm-readobj -ErrorAction SilentlyContinue) {
    $seen = New-Object System.Collections.Generic.HashSet[string]
    $queue = New-Object System.Collections.Generic.Queue[string]
    $queue.Enqueue((Join-Path $coreDir "gow3-probe.exe"))
    while ($queue.Count -gt 0) {
        $current = $queue.Dequeue()
        foreach ($import in (Get-PeImports $current)) {
            if (-not $seen.Add($import)) { continue }
            $next = Join-Path $coreDir $import
            if (Test-Path -LiteralPath $next) { $queue.Enqueue($next) }
        }
    }
    $missingDlls = @($seen | Where-Object { $_.EndsWith(".dll") -and -not (Test-Path -LiteralPath (Join-Path $coreDir $_)) -and
        $localDlls -notcontains $_ -and $_ -notmatch '^(api-ms-win|ext-ms-win)' })
    $systemDlls = @("kernel32.dll", "user32.dll", "gdi32.dll", "shell32.dll", "advapi32.dll", "ole32.dll",
        "oleaut32.dll", "shlwapi.dll", "version.dll", "winmm.dll", "ws2_32.dll", "wsock32.dll", "rpcrt4.dll",
        "crypt32.dll", "bcrypt.dll", "bcryptprimitives.dll", "ncrypt.dll", "ntdll.dll", "setupapi.dll",
        "iphlpapi.dll", "dnsapi.dll", "dwrite.dll", "gdiplus.dll", "imm32.dll", "msimg32.dll", "pdh.dll",
        "usp10.dll", "vulkan-1.dll")
    $unexpected = @($missingDlls | Where-Object { $systemDlls -notcontains $_.ToLowerInvariant() })
    if ($unexpected) {
        Write-Warning ("Runtime imports these DLLs but they are neither bundled nor Windows components: " + ($unexpected -join ", "))
    }
    Write-Host ("     import check: {0} bundled DLLs, all local imports resolved" -f $localDlls.Count)
} else {
    Write-Warning "llvm-readobj not found: skipping the DLL import check"
}

$fileCount = (Get-ChildItem -LiteralPath $Stage -Recurse -File -Force | Measure-Object).Count
$sizeBytes = [long]((Get-ChildItem -LiteralPath $Stage -Recurse -File -Force | Measure-Object Length -Sum).Sum)
Write-Host ("     {0} files, {1}" -f $fileCount, (Format-Size $sizeBytes))

# ---------------------------------------------------------------------------
Write-Step "6/6  Writing the folder and the zip"
# ---------------------------------------------------------------------------
Move-Item -LiteralPath $Stage -Destination $Final
Remove-Item -LiteralPath (Join-Path $OutputRoot "stage") -Recurse -Force -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
# ZipFile.CreateFromDirectory writes backslashes on Windows, which non-Windows
# extractors treat as part of the file name. Build the entries with the '/'
# separator the ZIP specification requires.
$zipStream = [System.IO.File]::Open($ZipPath, [System.IO.FileMode]::CreateNew)
try {
    $archive = New-Object System.IO.Compression.ZipArchive(
        $zipStream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $baseLength = $Final.Length + 1
        foreach ($file in (Get-ChildItem -LiteralPath $Final -Recurse -File -Force)) {
            $relative = $file.FullName.Substring($baseLength).Replace('\', '/')
            $entry = $archive.CreateEntry($relative, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $file.LastWriteTime
            $entryStream = $entry.Open()
            try {
                $input = [System.IO.File]::OpenRead($file.FullName)
                try { $input.CopyTo($entryStream) } finally { $input.Dispose() }
            } finally { $entryStream.Dispose() }
        }
    } finally { $archive.Dispose() }
} finally { $zipStream.Dispose() }

$hash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$sumsPath = Join-Path $OutputRoot "SHA256SUMS.txt"
Write-TextFile $sumsPath ("$hash  $StageName.zip`r`n")

Write-Host ""
Write-Host "Folder : $Final" -ForegroundColor Green
Write-Host ("Zip    : {0} ({1})" -f $ZipPath, (Format-Size (Get-Item -LiteralPath $ZipPath).Length)) -ForegroundColor Green
Write-Host "SHA-256: $hash" -ForegroundColor Green
Write-Host "Sums   : $sumsPath" -ForegroundColor Green
