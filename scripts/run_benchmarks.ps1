# Benchmark Runner Script
# Compares ts-aot compiled executables against Node.js
#
# Usage:
#   .\scripts\run_benchmarks.ps1
#   .\scripts\run_benchmarks.ps1 -Category startup
#   .\scripts\run_benchmarks.ps1 -Verbose

param(
    [string]$Category = "all",
    [switch]$Verbose,
    [int]$Runs = 10
)

$ErrorActionPreference = "Stop"

# Configuration
$CompilerPath = "build\src\compiler\Release\ts-aot.exe"
$ExamplesDir = "examples\benchmarks"
$OutputDir = "benchmark_results"

# Ensure output directory exists
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

# Benchmarks to run
$Benchmarks = @{
    "startup" = @(
        @{ Name = "cold_start"; File = "startup\cold_start.ts" }
        @{ Name = "cli_tool"; File = "startup\cli_tool.ts"; Args = "hello world" }
    )
    "compute" = @(
        @{ Name = "fibonacci"; File = "compute\fibonacci.ts" }
        @{ Name = "sorting"; File = "compute\sorting.ts" }
        @{ Name = "json_parse"; File = "compute\json_parse.ts" }
        @{ Name = "regex"; File = "compute\regex.ts" }
    )
    "io" = @(
        @{ Name = "file_copy"; File = "io\file_copy.ts" }
    )
    "memory" = @(
        @{ Name = "allocation"; File = "memory\allocation.ts" }
        @{ Name = "gc_stress"; File = "memory\gc_stress.ts" }
    )
}

# Results storage
$Results = @()

function Write-Header {
    param([string]$Text)
    Write-Host ""
    Write-Host "=" * 60 -ForegroundColor Cyan
    Write-Host " $Text" -ForegroundColor Cyan
    Write-Host "=" * 60 -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Text)
    Write-Host "  -> $Text" -ForegroundColor Yellow
}

function Measure-Execution {
    param(
        [string]$Command,
        [string]$Args = "",
        [int]$Iterations = 10
    )

    $times = @()

    for ($i = 0; $i -lt $Iterations; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        if ($Args) {
            & $Command $Args.Split(" ") 2>&1 | Out-Null
        } else {
            & $Command 2>&1 | Out-Null
        }
        $sw.Stop()
        $times += $sw.ElapsedMilliseconds
    }

    $sorted = $times | Sort-Object
    $avg = ($times | Measure-Object -Average).Average

    return @{
        Min = $sorted[0]
        Max = $sorted[-1]
        Avg = [math]::Round($avg, 2)
        P50 = $sorted[[math]::Floor($Iterations * 0.5)]
        P95 = $sorted[[math]::Floor($Iterations * 0.95)]
    }
}

function Build-Benchmark {
    param(
        [string]$SourceFile,
        [string]$OutputFile
    )

    if ($Verbose) {
        Write-Step "Building: $SourceFile"
    }

    $result = & $CompilerPath $SourceFile -o $OutputFile 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed for $SourceFile" -ForegroundColor Red
        Write-Host $result
        return $false
    }

    return $true
}

function Run-Benchmark {
    param(
        [hashtable]$Benchmark
    )

    $name = $Benchmark.Name
    $sourceFile = Join-Path $ExamplesDir $Benchmark.File
    $exeFile = Join-Path $OutputDir "$name.exe"
    $args = if ($Benchmark.Args) { $Benchmark.Args } else { "" }

    Write-Host ""
    Write-Host "Benchmark: $name" -ForegroundColor Green

    # Build ts-aot version
    $buildSuccess = Build-Benchmark -SourceFile $sourceFile -OutputFile $exeFile
    if (!$buildSuccess) {
        return $null
    }

    # Measure ts-aot
    Write-Step "Running ts-aot version ($Runs iterations)..."
    $aotResults = Measure-Execution -Command $exeFile -Args $args -Iterations $Runs

    # Measure Node.js
    Write-Step "Running Node.js version ($Runs iterations)..."
    $nodeResults = Measure-Execution -Command "node" -Args $sourceFile -Iterations $Runs

    # Calculate speedup
    $speedup = if ($aotResults.Avg -gt 0) {
        [math]::Round($nodeResults.Avg / $aotResults.Avg, 2)
    } else {
        "N/A"
    }

    # Report results
    Write-Host "  Results:" -ForegroundColor White
    Write-Host "    ts-aot:  avg=$($aotResults.Avg)ms, p50=$($aotResults.P50)ms, p95=$($aotResults.P95)ms"
    Write-Host "    Node.js: avg=$($nodeResults.Avg)ms, p50=$($nodeResults.P50)ms, p95=$($nodeResults.P95)ms"
    Write-Host "    Speedup: ${speedup}x" -ForegroundColor $(if ($speedup -gt 1) { "Green" } else { "Yellow" })

    return @{
        Name = $name
        AOT = $aotResults
        Node = $nodeResults
        Speedup = $speedup
    }
}

# Main execution
Write-Header "ts-aot Benchmark Suite"
Write-Host "Runs per benchmark: $Runs"
Write-Host "Category: $Category"

# Check compiler exists
if (!(Test-Path $CompilerPath)) {
    Write-Host "Compiler not found at $CompilerPath" -ForegroundColor Red
    Write-Host "Please build the compiler first: cmake --build build --config Release"
    exit 1
}

# Get categories to run
$categoriesToRun = if ($Category -eq "all") {
    $Benchmarks.Keys
} else {
    @($Category)
}

# Run benchmarks
foreach ($cat in $categoriesToRun) {
    if (!$Benchmarks.ContainsKey($cat)) {
        Write-Host "Unknown category: $cat" -ForegroundColor Red
        continue
    }

    Write-Header "Category: $cat"

    foreach ($benchmark in $Benchmarks[$cat]) {
        $result = Run-Benchmark -Benchmark $benchmark
        if ($result) {
            $Results += $result
        }
    }
}

# Summary table
Write-Header "Summary"

$tableFormat = "{0,-20} | {1,12} | {2,12} | {3,10}"
Write-Host ($tableFormat -f "Benchmark", "ts-aot (ms)", "Node.js (ms)", "Speedup")
Write-Host ("-" * 60)

foreach ($result in $Results) {
    Write-Host ($tableFormat -f $result.Name, $result.AOT.Avg, $result.Node.Avg, "$($result.Speedup)x")
}

Write-Host ("-" * 60)

# Calculate overall averages
$avgAotTime = ($Results.AOT.Avg | Measure-Object -Average).Average
$avgNodeTime = ($Results.Node.Avg | Measure-Object -Average).Average
$avgSpeedup = ($Results | Where-Object { $_.Speedup -ne "N/A" } | ForEach-Object { $_.Speedup } | Measure-Object -Average).Average

Write-Host ""
Write-Host "Average ts-aot time:  $([math]::Round($avgAotTime, 2))ms"
Write-Host "Average Node.js time: $([math]::Round($avgNodeTime, 2))ms"
Write-Host "Average speedup:      $([math]::Round($avgSpeedup, 2))x"

# Save results to JSON
$outputFile = Join-Path $OutputDir "results_$(Get-Date -Format 'yyyyMMdd_HHmmss').json"
$Results | ConvertTo-Json -Depth 10 | Set-Content $outputFile
Write-Host ""
Write-Host "Results saved to: $outputFile" -ForegroundColor Cyan
