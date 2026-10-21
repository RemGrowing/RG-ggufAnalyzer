param(
    [Parameter(Mandatory=$true)][string]$InputFile,
    [string]$OutputFile = 'gguf_report.json',
    [ValidateRange(1,256)][int]$ChunkMiB = 64,
    [ValidateSet('auto','cpu','cuda')][string]$Backend = 'auto',
    [switch]$NoHash
)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'build\Release\gguf_analyzer.exe'
if (-not (Test-Path $exe)) { $exe = Join-Path $PSScriptRoot 'build\gguf_analyzer.exe' }
if (-not (Test-Path $exe)) { throw "Executable not found under build. Run .\build.ps1 first." }
if (-not (Test-Path -LiteralPath $InputFile -PathType Leaf)) { throw "Input file not found: $InputFile" }
$argsList = @((Resolve-Path -LiteralPath $InputFile).Path, '--out', $OutputFile, '--chunk-mib', "$ChunkMiB", '--backend', $Backend)
if ($NoHash) { $argsList += '--no-hash' }
& $exe @argsList
exit $LASTEXITCODE
