# --- create (or clean) the out‑of‑tree build directory ---
# Determine Avogadro source root as parent of script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$AvoRoot = Resolve-Path (Join-Path $ScriptDir '..')
$BuildDir = Join-Path $AvoRoot 'build'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Location $BuildDir

# ───────────────────────────────────────────────────────────────
# 1.  Ensure LIBS_HOME is defined for this session
# ───────────────────────────────────────────────────────────────
if (-not $env:LIBS_HOME -or [string]::IsNullOrWhiteSpace($env:LIBS_HOME)) {
    $env:LIBS_HOME = Join-Path $AvoRoot 'libs'
}

# ───────────────────────────────────────────────────────────────
# 2.  Locate third‑party prefixes and expose them to CMake
# ───────────────────────────────────────────────────────────────

# 2‑A · Qt 5.15.2 ------------------------------------------------
$qtRoot     = Join-Path $env:LIBS_HOME "Qt\5.15.2\msvc2019_64"
$qtBins     = "$qtRoot\bin"
$qtCMakeDir = "$qtRoot\lib\cmake"          # contains Qt5Config.cmake

if (-not (Test-Path $qtRoot)) {
    throw "Qt root not found at $qtRoot – run install_libs.ps1 first."
}

# add moc/rcc/uic to PATH so FindQt5 succeeds
if (-not $env:PATH.Split(';') -contains $qtBins) {
    $env:PATH = "$qtBins;${env:PATH}"
}

# 2‑B · Eigen 3.4.0 ---------------------------------------------
$eigenDir = Join-Path $env:LIBS_HOME "eigen-3.4.0"   # header‑only
if (-not (Test-Path "$eigenDir/Eigen")) {
    Write-Warning "Eigen headers not found at $eigenDir – run install_libs.ps1 again."
}


$libxml2Prefix   = "${env:LIBS_HOME}/libxml2-2.12.10/install"

# 2-C · Build master prefix list for CMake (Qt5 and libxml2)
$thirdPartyPrefixes = @( $qtRoot, $qtCMakeDir, $libxml2Prefix ) -join ';'
$env:CMAKE_PREFIX_PATH = "$thirdPartyPrefixes;${env:CMAKE_PREFIX_PATH}"

# ───────────────────────────────────────────────────────────────
# 3.  Configure Avogadro
# ───────────────────────────────────────────────────────────────
cmake -G "NMake Makefiles" .. `
  "-DCMAKE_BUILD_TYPE=Release" `
  "-DCMAKE_INSTALL_PREFIX=${HOME}/build/install" `
  "-DZLIB_ROOT=${env:LIBS_HOME}/zlib-1.3.1/install" `
  "-DEIGEN3_INCLUDE_DIR=${env:LIBS_HOME}/eigen-3.4.0" `
  "-DLIBXML2_INCLUDE_DIR=${env:LIBS_HOME}/libxml2-2.12.10/install/include" `
  "-DLIBXML2_LIBRARIES=${env:LIBS_HOME}/libxml2-2.12.10/install/lib/libxml2.lib" `
  "-DGLEW_INCLUDE_DIR=${env:LIBS_HOME}/glew-2.2.0/install/include" `
  "-DGLEW_LIBRARY=${env:LIBS_HOME}/glew-2.2.0/install/lib/glew32.lib" `
  "-DCMAKE_PREFIX_PATH=$thirdPartyPrefixes" `
  "-DENABLE_GLSL=ON" `
  "-DENABLE_TESTS=ON"

# ───────────────────────────────────────────────────────────────
# 4.  Build + install
# ───────────────────────────────────────────────────────────────

# Build and install
cmake --build . --config Release --target install

# Run tests
ctest -C Release --output-on-failure

