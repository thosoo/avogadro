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
$LIBS_HOME = Join-Path $HOME 'libs'
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

function Build-CMake {
    param([string]$Src,[string]$Bld,[string]$Prefix,[string[]]$Args)
    if (Test-Path "$Bld/CMakeCache.txt") {
        if ((Get-Content "$Bld/CMakeCache.txt" -Raw) -match '\$env:LIBS_HOME') { Remove-Item -Recurse -Force $Bld }
    }
    if ($Force -and (Test-Path $Bld)) { Remove-Item -Recurse -Force $Bld }
    cmake -S $Src -B $Bld -G "NMake Makefiles" "-DCMAKE_INSTALL_PREFIX=${Prefix}" @Args
    cmake --build $Bld --target install --config Release
}

# ── zlib 1.3.1 ─────────────────────────────────────────────────────────────
Get-Archive "https://zlib.net/zlib131.zip" "$LIBS_HOME/zlib-1.3.1"
$zlibPrefix   = "$LIBS_HOME/zlib-1.3.1/install"
$zlibInclude  = "$zlibPrefix/include"
$zlibLibDir   = "$zlibPrefix/lib"
Build-CMake "$LIBS_HOME/zlib-1.3.1" "$LIBS_HOME/zlib-build" $zlibPrefix `
           -Args "-DBUILD_SHARED_LIBS=OFF","-DCMAKE_BUILD_TYPE=Release"

# ── Eigen 3.4.0 (header‑only) ──────────────────────────────────────────────
Get-Archive "https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip" "$LIBS_HOME/eigen-3.4.0"

# ── libxml2 2.12.10 (MSVC Makefile) ───────────────────────────────────────
$libxmlVer = '2.12.10'
$libxmlSrc = "$LIBS_HOME/libxml2-$libxmlVer"
if ($Force -or -not (Test-Path "$libxmlSrc/install/lib/libxml2.lib")) {
    # fresh download because GitHub zip keeps unix permissions intact
    Get-Archive "https://github.com/GNOME/libxml2/archive/refs/tags/v$libxmlVer.zip" $libxmlSrc "libxml2-$libxmlVer.zip"
    $installDir = Join-Path $libxmlSrc 'install'
    $win32Dir   = Join-Path $libxmlSrc 'win32'

    Push-Location $win32Dir
    # provide zlib paths explicitly
    $env:ZLIB_INCLUDE_DIR = $zlibInclude
    $env:ZLIB_LIBRARY_DIR = $zlibLibDir
    cscript configure.js compiler=msvc prefix=$installDir iconv=no zlib=yes include=$env:ZLIB_INCLUDE_DIR lib=$env:ZLIB_LIBRARY_DIR

    # tweak Makefile: remove /OPT:NOWIN98 and link against zlib.lib
    (Get-Content Makefile.msvc) -replace '/OPT:NOWIN98','' -replace 'zdll.lib','zlib.lib' | Set-Content Makefile.msvc

    # work around snprintf/vsnprintf re‑defines breaking MSVC
    $conf = Join-Path $libxmlSrc 'config.h'
    if (Test-Path $conf) {
        (Get-Content $conf) -replace '^#define snprintf.*','// removed snprintf define' -replace '^#define vsnprintf.*','// removed vsnprintf define' | Set-Content $conf
    }

    nmake /f Makefile.msvc
    nmake /f Makefile.msvc install
    Pop-Location
} else {
    Write-Host "✓ libxml2 already built – skip"
}

# ── GLEW 2.2.0 ─────────────────────────────────────────────────────────────
Get-Archive "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip" "$LIBS_HOME/glew-2.2.0"
$glewPrefix = "$LIBS_HOME/glew-2.2.0/install"
Build-CMake "$LIBS_HOME/glew-2.2.0/build/cmake" "$LIBS_HOME/glew-build" $glewPrefix `
           -Args "-DBUILD_SHARED_LIBS=ON","-DBUILD_UTILS=OFF","-DCMAKE_BUILD_TYPE=Release"

# ── Qt 5.15.2 via aqtinstall ──────────────────────────────────────────────
if (-not (Get-Command aqt -ErrorAction SilentlyContinue)) { python -m pip install --upgrade aqtinstall }
$qtPrefix = "$LIBS_HOME/Qt/5.15.2/msvc2019_64"
if ($Force -or -not (Test-Path $qtPrefix)) {
    python -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O "$LIBS_HOME/Qt" -m qtcharts qtscript
} else { Write-Host "✓ Qt already present – skip" }

Write-Host "`n🎉  All libraries are now (re)built under $LIBS_HOME"

