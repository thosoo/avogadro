
$distBin = Join-Path $pwd 'scripts/installer/dist/bin'
$root    = if ($Env:ONEAPI_ROOT) { $Env:ONEAPI_ROOT } else { 'C:\Program Files (x86)\Intel\oneAPI' }


# DLLs are placed under <ONEAPI_ROOT>/2024.1/bin on the runners
$dllDir = Join-Path $root '2024.1\bin'
if (-not (Test-Path $dllDir)) { throw 'Intel redist directory not found' }

Copy-Item -Path (Join-Path $dllDir '*.dll') -Destination $distBin -Recurse -Force
