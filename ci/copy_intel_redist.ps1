

$distBin = Join-Path $pwd 'scripts/installer/dist/bin'
$root    = if ($Env:ONEAPI_ROOT) { $Env:ONEAPI_ROOT } else { 'C:\Program Files (x86)\Intel\oneAPI' }

# Copy the required runtime DLLs from the 2024.1 bin directory
$binDir = Join-Path $root '2024.1\bin'
if (-not (Test-Path $binDir)) {
    throw 'Intel redist directory not found'
}

$dlls = @(
    'libifcoremd.dll',
    'libifcorert.dll',
    'libifportmd.dll',
    'libmmd.dll',
    'libirc.dll',
    'svml_dispmd.dll',
    'libiomp5md.dll',
    'mkl_rt.dll'
)
foreach ($dll in $dlls) {
    $path = Join-Path $binDir $dll
    if (Test-Path $path) {
        Copy-Item $path $distBin -Force
    }
}

