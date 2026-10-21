$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$engine = Join-Path $root 'build_cpu\Release\gguf_analyzer.exe'
if (-not (Test-Path $engine)) { $engine = Join-Path $root 'build_cpu\gguf_analyzer.exe' }
if (-not (Test-Path $engine)) { throw 'Engine executable not found. Run ..\build.ps1 first.' }
$fixture = Join-Path $root 'tests\minimal.gguf'
$out = Join-Path $env:TEMP 'gguf_analyzer_smoke_report.json'
Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
& $engine $fixture --out $out --chunk-mib 1 --backend cpu
$rc = $LASTEXITCODE
if ($rc -ne 0) { throw "CPU smoke test failed with exit code $rc" }
if (-not (Test-Path -LiteralPath $out -PathType Leaf)) { throw 'Report JSON was not created.' }
$report = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json
if ($report.physical_scan.coverage_percent -ne 100) { throw 'Expected 100% scan coverage.' }
if ($report.backend.active -ne 'cpu') { throw 'Expected CPU backend.' }
Write-Host 'CPU smoke test passed.' -ForegroundColor Green
Write-Host "Report: $out"

& $engine $fixture --backend cuda 2>$null
$cudaExit = $LASTEXITCODE
if ($cudaExit -eq 0) { throw 'CPU-only build unexpectedly accepted CUDA backend.' }
Write-Host "CUDA rejection test passed (exit code $cudaExit)." -ForegroundColor Green
