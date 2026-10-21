param(
    [switch]$BuildGuiExe,
    [switch]$SkipEngineBuild
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$engine = Join-Path $root 'engine'
$buildDir = Join-Path $engine 'build_cpu'

if (-not $SkipEngineBuild) {
    Write-Host '=== [1/3] Configure CPU-only C++ engine ===' -ForegroundColor Cyan
    cmake -S $engine -B $buildDir -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    Write-Host '=== [2/3] Build C++ engine ===' -ForegroundColor Cyan
    cmake --build $buildDir --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'C++ engine build failed.' }
}

$exeCandidates = @(
    (Join-Path $buildDir 'Release\gguf_analyzer.exe'),
    (Join-Path $buildDir 'gguf_analyzer.exe')
)
$engineExe = $exeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $engineExe) { throw 'C++ engine executable not found. Check the build output.' }
$bundleEngineDir = Join-Path $buildDir 'Release'
New-Item -ItemType Directory -Force -Path $bundleEngineDir | Out-Null
$bundleEngine = Join-Path $bundleEngineDir 'gguf_analyzer.exe'
if ([System.IO.Path]::GetFullPath($engineExe) -ne [System.IO.Path]::GetFullPath($bundleEngine)) {
    Copy-Item -LiteralPath $engineExe -Destination $bundleEngine -Force
}
Write-Host "Engine built: $bundleEngine" -ForegroundColor Green

if ($BuildGuiExe) {
    Write-Host '=== [3/3] Build GUI executable with PyInstaller ===' -ForegroundColor Cyan
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { throw 'Python was not found in PATH.' }
    python -m pip install --upgrade pyinstaller
    if ($LASTEXITCODE -ne 0) { throw 'Could not install PyInstaller.' }

    $binarySpec = "$bundleEngine;engine/build_cpu/Release"
    python -m PyInstaller --noconfirm --clean --windowed --onefile `
        --name 'GGUF Analyzer' `
        --icon (Join-Path $root 'assets\orca_logo.ico') `
        --add-data (Join-Path $root 'assets\orca_logo.png') 'assets' `
        --add-binary $binarySpec `
        --distpath (Join-Path $root 'dist') `
        --workpath (Join-Path $root 'build_pyinstaller') `
        --specpath $root `
        (Join-Path $root 'gui.py')
    if ($LASTEXITCODE -ne 0) { throw 'PyInstaller GUI build failed.' }
    Write-Host "Single-file GUI built: $(Join-Path $root 'dist\GGUF Analyzer.exe')" -ForegroundColor Green
}
else {
    Write-Host 'GUI EXE build skipped. Use -BuildGuiExe to package the Python GUI.' -ForegroundColor Yellow
}
