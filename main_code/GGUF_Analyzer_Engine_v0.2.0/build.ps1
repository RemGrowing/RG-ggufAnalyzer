param(
    [ValidateSet('Auto','CPU','CUDA')][string]$Backend = 'Auto',
    [string]$BuildDir = 'build'
)
$ErrorActionPreference = 'Stop'
$enableCuda = if ($Backend -eq 'CPU') { 'OFF' } else { 'ON' }
cmake -S . -B $BuildDir -DGGUF_ENABLE_CUDA=$enableCuda -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host "Build completed. CUDA auto-detection requested: $enableCuda"
