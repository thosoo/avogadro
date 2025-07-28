<#
    install_libs.ps1 – one‑stop installer for the Avogadro (thosoo fork) third‑party stack
    ────────────────────────────────────────────────────────────────────────────
    ▸ Installs **zlib 1.3.1**, **Eigen 3.4.0**, **libxml2 2.12.10**, **GLEW 2.2.0** and **Qt 5.15.2**
      into **%USERPROFILE%\libs** so nothing touches your repo checkout.
    ▸ Run from an **x64 Developer PowerShell for VS 2022** window.
    ▸ Re‑run with **‑Force** to wipe & rebuild any dependency.
#>

[CmdletBinding()]
param([switch]$Force)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

# ── Globals ────────────────────────────────────────────────────────────────
# Determine Avogadro source root as parent of script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$AvoRoot = Resolve-Path (Join-Path $ScriptDir '..')
$LIBS_HOME = Join-Path $AvoRoot 'libs'
[Environment]::SetEnvironmentVariable('LIBS_HOME', $LIBS_HOME, 'User')
New-Item -ItemType Directory -Path $LIBS_HOME -Force | Out-Null

function Get-Archive {
    param([string]$Url,[string]$DestDir,[string]$Name=(Split-Path $Url -Leaf))
    if (-not $Force -and (Test-Path $DestDir)) { Write-Host "✓ $DestDir already present – skip" ; return }
    $tmp = Join-Path $env:TEMP $Name
    Write-Host "→ Downloading $Name …"
    Invoke-WebRequest $Url -OutFile $tmp -UseBasicParsing
    Write-Host "→ Extracting …"
    if ($tmp -match '\.zip$') { Expand-Archive -LiteralPath $tmp -DestinationPath $LIBS_HOME -Force }
    else                       { tar -xf $tmp -C $LIBS_HOME }
}


# ── zlib 1.3.1 ─────────────────────────────────────────────────────────────




# Download and build zlib Release only
Get-Archive "https://zlib.net/zlib131.zip" "$LIBS_HOME/zlib-1.3.1"
$zlibPrefixRelease = "$LIBS_HOME/zlib-1.3.1/install-release"
$zlibInclude       = "$LIBS_HOME/zlib-1.3.1/install/include"  # Common include dir for downstream
$zlibLibDir        = "$LIBS_HOME/zlib-1.3.1/install/lib"      # Common lib dir for downstream

# Build zlib Release
$zlibBuildRelease = "$LIBS_HOME/zlib-build-release"
if (Test-Path $zlibBuildRelease) { Remove-Item -Recurse -Force $zlibBuildRelease }
if (Test-Path $zlibPrefixRelease) { Remove-Item -Recurse -Force $zlibPrefixRelease }
Write-Host "→ Configuring $zlibBuildRelease for Release build..."
cmake -S "$LIBS_HOME/zlib-1.3.1" -B $zlibBuildRelease -G "NMake Makefiles" -DCMAKE_INSTALL_PREFIX="$zlibPrefixRelease" -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build $zlibBuildRelease --target install --config Release
# Copy Release artifacts to common location
$zlibStaticLibRelease = Join-Path $zlibPrefixRelease 'lib/zlibstatic.lib'
$zlibLibRelease = Join-Path $zlibLibDir 'zlib.lib'
if (Test-Path $zlibStaticLibRelease) {
    New-Item -ItemType Directory -Path $zlibLibDir -Force | Out-Null
    Copy-Item $zlibStaticLibRelease $zlibLibRelease -Force
    Write-Host "✓ Copied Release zlibstatic.lib to common zlib.lib for downstream consumers."
} else {
    Write-Warning "zlibstatic.lib not found after Release build; libxml2 may fail to link."
}
# Copy include files from Release install
$zlibIncludeRelease = Join-Path $zlibPrefixRelease 'include'
if (Test-Path $zlibIncludeRelease) {
    New-Item -ItemType Directory -Path $zlibInclude -Force | Out-Null
    Copy-Item "$zlibIncludeRelease\*" $zlibInclude -Recurse -Force
    Write-Host "✓ Copied zlib include files to common include dir."
} else {
    Write-Warning "zlib include directory not found after Release build."
}

# ── Eigen 3.4.0 (header‑only) ──────────────────────────────────────────────
Get-Archive "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip" "$LIBS_HOME/eigen-3.4.0"

# ── libxml2 2.12.10 (MSVC Makefile) ───────────────────────────────────────
$libxmlVer = '2.12.10'
$libxmlSrc = "$LIBS_HOME/libxml2-$libxmlVer"
$libxmlLib = "$libxmlSrc/install/lib/libxml2.lib"
if ($Force -or -not (Test-Path $libxmlLib)) {
    # fresh download because GitHub zip keeps unix permissions intact
    Get-Archive "https://github.com/GNOME/libxml2/archive/refs/tags/v$libxmlVer.zip" $libxmlSrc "libxml2-$libxmlVer.zip"
    $installDir = Join-Path $libxmlSrc 'install'
    $win32Dir   = Join-Path $libxmlSrc 'win32'

    Push-Location $win32Dir
    # provide zlib paths explicitly
    $env:ZLIB_INCLUDE_DIR = $zlibInclude
    $env:ZLIB_LIBRARY_DIR = $zlibLibDir
    Write-Host "→ Configuring libxml2 with zlib include=$env:ZLIB_INCLUDE_DIR lib=$env:ZLIB_LIBRARY_DIR"
    $confResult = cscript configure.js compiler=msvc prefix=$installDir iconv=no zlib=yes include=$env:ZLIB_INCLUDE_DIR lib=$env:ZLIB_LIBRARY_DIR 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Error "libxml2 configure.js failed: $confResult"
        exit 1
    }

    # tweak Makefile: remove /OPT:NOWIN98 and link against zlib.lib
    (Get-Content Makefile.msvc) -replace '/OPT:NOWIN98','' -replace 'zdll.lib','zlib.lib' | Set-Content Makefile.msvc

    # work around snprintf/vsnprintf re‑defines breaking MSVC
    $conf = Join-Path $libxmlSrc 'config.h'
    if (Test-Path $conf) {
        (Get-Content $conf) -replace '^#define snprintf.*','// removed snprintf define' -replace '^#define vsnprintf.*','// removed vsnprintf define' | Set-Content $conf
    }

    Write-Host "→ Diagnostic: Current directory: $(Get-Location)"
    Write-Host "→ Diagnostic: ZLIB_INCLUDE_DIR=$env:ZLIB_INCLUDE_DIR"
    Write-Host "→ Diagnostic: ZLIB_LIBRARY_DIR=$env:ZLIB_LIBRARY_DIR"
    Write-Host '→ Diagnostic: First 20 lines of Makefile.msvc:'
    Get-Content Makefile.msvc -TotalCount 20 | ForEach-Object { Write-Host $_ }
    Write-Host '→ Diagnostic: Last 20 lines of Makefile.msvc:'
    Get-Content Makefile.msvc | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
    if (Test-Path $conf) {
        Write-Host '→ Diagnostic: First 20 lines of config.h:'
        Get-Content $conf -TotalCount 20 | ForEach-Object { Write-Host $_ }
        Write-Host '→ Diagnostic: Last 20 lines of config.h:'
        Get-Content $conf | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
    }
    Write-Host "→ Building libxml2..."
    nmake /f Makefile.msvc
    if ($LASTEXITCODE -ne 0) {
        Write-Error "libxml2 build failed. See output above."
        exit 1
    }
    Write-Host "→ Installing libxml2..."
    nmake /f Makefile.msvc install
    if ($LASTEXITCODE -ne 0) {
        Write-Error "libxml2 install failed. See output above."
        exit 1
    }
    Pop-Location
    if (-not (Test-Path $libxmlLib)) {
        Write-Error "libxml2 install did not produce $libxmlLib. Check build logs above."
        exit 1
    } else {
        Write-Host "✓ libxml2 built and installed: $libxmlLib"
    }
} else {
    Write-Host "✓ libxml2 already built – skip"
}


# ── GLEW 2.2.0 ─────────────────────────────────────────────────────────────
Get-Archive "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip" "$LIBS_HOME/glew-2.2.0"
$glewPrefix = "$LIBS_HOME/glew-2.2.0/install"
$glewBuild = "$LIBS_HOME/glew-build"
if (Test-Path $glewBuild) { Remove-Item -Recurse -Force $glewBuild }
if (Test-Path $glewPrefix) { Remove-Item -Recurse -Force $glewPrefix }
Write-Host "→ Configuring $glewBuild for Release build..."
cmake -S "$LIBS_HOME/glew-2.2.0/build/cmake" -B $glewBuild -G "NMake Makefiles" -DCMAKE_INSTALL_PREFIX="$glewPrefix" -DBUILD_SHARED_LIBS=ON -DBUILD_UTILS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build $glewBuild --target install --config Release


# ── Qt 5.15.2 via aqtinstall ──────────────────────────────────────────────
if (-not (Get-Command aqt -ErrorAction SilentlyContinue)) { python -m pip install --upgrade aqtinstall }
$qtPrefix = Join-Path $LIBS_HOME 'Qt/5.15.2/msvc2019_64'
[Environment]::SetEnvironmentVariable('LIBS_QT_ROOT', $qtPrefix, 'User')
if ($Force -or -not (Test-Path $qtPrefix)) {
    $origDir = Get-Location
    Set-Location $LIBS_HOME
    try {
        # Ensure aqtinstall.log is created in $LIBS_HOME
        python -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O "$LIBS_HOME/Qt" -m qtcharts qtscript
    } finally {
        Set-Location $origDir
    }
} else { Write-Host "✓ Qt already present – skip" }

Write-Host "`n🎉  All libraries are now (re)built under $LIBS_HOME"

