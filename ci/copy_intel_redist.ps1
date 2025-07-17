$distBin = Join-Path $pwd 'scripts/installer/dist/bin'
$root    = if ($Env:ONEAPI_ROOT) { $Env:ONEAPI_ROOT } else { 'C:\Program Files (x86)\Intel\oneAPI' }

$candidates = Get-ChildItem (Join-Path $root 'compiler') -Directory |
    ForEach-Object {
        @( Join-Path $_ 'windows\redist\intel64_win\compiler' )
        @( Join-Path $_ 'windows\redist\intel64\compiler'      )
    }

$dllDir = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dllDir) { throw 'Intel redist directory not found' }

Copy-Item -Path (Join-Path $dllDir '*.dll') -Destination $distBin -Recurse -Force
