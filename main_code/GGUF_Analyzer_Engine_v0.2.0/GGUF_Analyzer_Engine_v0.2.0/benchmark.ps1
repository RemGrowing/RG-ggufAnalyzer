param(
    [Parameter(Mandatory=$true)][string]$InputFile,
    [ValidateRange(1,256)][int]$ChunkMiB = 64,
    [string]$OutputDirectory = 'benchmark-results'
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'build\Release\gguf_analyzer.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $PSScriptRoot 'build\gguf_analyzer.exe' }
if (-not (Test-Path $exe)) { throw 'Executable not found. Run .\build.ps1 first.' }
if (-not (Test-Path -LiteralPath $InputFile -PathType Leaf)) { throw "Input file not found: $InputFile" }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$input = (Resolve-Path -LiteralPath $InputFile).Path
$results = @()
foreach ($backend in @('cpu','cuda')) {
    $report = Join-Path $OutputDirectory "$backend.json"
    & $exe $input --backend $backend --chunk-mib $ChunkMiB --no-hash --out $report
    if ($LASTEXITCODE -ne 0) { throw "Backend $backend failed with exit code $LASTEXITCODE" }
    $j = Get-Content -Raw $report | ConvertFrom-Json
    $results += [pscustomobject]@{
        RequestedBackend = $j.backend.requested
        ActiveBackend = $j.backend.active
        CudaAvailable = $j.backend.cuda_available
        Device = $j.backend.gpu_device
        BytesScanned = $j.physical_scan.bytes_scanned
        CoveragePercent = $j.physical_scan.coverage_percent
        ScanSeconds = $j.performance.scan_seconds
        MiBPerSecond = $j.performance.scan_mib_per_second
        GpuChunks = $j.backend.gpu_chunks_processed
    }
}
$results | Format-Table -AutoSize
$results | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutputDirectory 'comparison.csv')
Write-Host "Reports and comparison CSV: $((Resolve-Path $OutputDirectory).Path)"
