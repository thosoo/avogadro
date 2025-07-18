$distBin = Join-Path $pwd 'scripts/installer/dist/bin'
$root    = if ($Env:ONEAPI_ROOT) { $Env:ONEAPI_ROOT } else { 'C:\Program Files (x86)\Intel\oneAPI' }

# First probe the "latest" compiler redist directory (preferred)
$dllDirCandidates = @(
    Join-Path $root 'compiler\latest\windows\redist\intel64_win\compiler'
    Join-Path $root 'compiler\latest\windows\redist\intel64\compiler'
)

# Fall back to scanning all compiler versions if none of the preferred paths exist
if (-not ($dllDirCandidates | Where-Object { Test-Path $_ })) {
    $dllDirCandidates = Get-ChildItem -Path (Join-Path $root 'compiler') -Directory | ForEach-Object {
        @( Join-Path $_ 'windows\redist\intel64_win\compiler' )
        @( Join-Path $_ 'windows\redist\intel64\compiler'      )
    }
}

$dllDir = $dllDirCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dllDir) { throw 'Intel redist directory not found' }

Copy-Item -Path (Join-Path $dllDir '*.dll') -Destination $distBin -Recurse -Force
