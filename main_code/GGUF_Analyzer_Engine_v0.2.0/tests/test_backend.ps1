$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\Release\gguf_analyzer.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $root 'build\gguf_analyzer.exe' }
if (-not (Test-Path $exe)) { throw 'Build the project before running tests.' }
$sample = Join-Path $PSScriptRoot 'minimal.gguf'
if (-not (Test-Path $sample)) { throw 'minimal.gguf test fixture is missing.' }
foreach ($backend in @('cpu','auto','cuda')) {
    $report = Join-Path $env:TEMP "gguf-test-$backend.json"
    & $exe $sample --backend $backend --no-hash --out $report
    if ($LASTEXITCODE -ne 0) { throw "Backend $backend returned exit code $LASTEXITCODE" }
    $j = Get-Content -Raw $report | ConvertFrom-Json
    if ($j.physical_scan.coverage_percent -ne 100) { throw "Coverage check failed for $backend" }
    if (($j.byte_statistics.histogram | Measure-Object -Sum).Sum -ne 32) { throw "Histogram sum check failed for $backend" }
    Remove-Item $report -Force
}
Write-Host 'Backend smoke tests passed.'
