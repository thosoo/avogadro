
$distBin = Join-Path $pwd 'scripts/installer/dist/bin'
$root    = if ($Env:ONEAPI_ROOT) { $Env:ONEAPI_ROOT } else { 'C:\Program Files (x86)\Intel\oneAPI' }

# Probe all compiler versions for the redistributable folder
$candidates = Get-ChildItem (Join-Path $root 'compiler') -Directory | ForEach-Object {
    @( Join-Path $_ 'windows\redist\intel64_win\compiler' )
    @( Join-Path $_ 'windows\redist\intel64\compiler' )
}

$dllDir = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dllDir) { throw 'Intel redist directory not found' }

$dlls = @(
    'libifcoremd.dll',
    'libifcorert.dll',
    'libifportmd.dll',
    'libmmd.dll',
    'libirc.dll',
    'svml_dispmd.dll',
    'libiomp5md.dll'
)
foreach ($dll in $dlls) {
    $path = Join-Path $dllDir $dll
    if (Test-Path $path) {
        Copy-Item $path $distBin -Force
    }
}

if ($Env:MKLROOT) {
    $mkl = Join-Path $Env:MKLROOT 'redist\intel64\mkl_rt.dll'
    if (Test-Path $mkl) { Copy-Item $mkl $distBin -Force }
}
