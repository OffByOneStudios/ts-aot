# JavaScript Pattern Test Runner
# Tests individual JavaScript patterns before attempting full lodash

param(
    [switch]$StopOnFail,
    [switch]$Verbose,
    [string]$Pattern = "*"
)

$ErrorActionPreference = "Continue"
$script:passed = 0
$script:failed = 0
$script:results = @()

$testDir = "tests\js_patterns"
$compiler = ".\build\src\compiler\Release\ts-aot.exe"

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  JavaScript Pattern Test Suite" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Get all JS test files
$testFiles = Get-ChildItem -Path $testDir -Filter "$Pattern.js" | Sort-Object Name

if ($testFiles.Count -eq 0) {
    Write-Host "No test files found matching pattern: $Pattern" -ForegroundColor Yellow
    exit 1
}

Write-Host "Found $($testFiles.Count) test files" -ForegroundColor Gray
Write-Host ""

foreach ($file in $testFiles) {
    $testName = $file.BaseName
    $jsPath = $file.FullName
    $exePath = $jsPath -replace '\.js$', '.exe'
    
    Write-Host "[$testName] " -NoNewline
    
    # Compile
    $compileOutput = & $compiler $jsPath -o $exePath 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "COMPILE FAILED" -ForegroundColor Red
        if ($Verbose) {
            Write-Host $compileOutput -ForegroundColor DarkGray
        }
        $script:failed++
        $script:results += [PSCustomObject]@{
            Test = $testName
            Status = "COMPILE FAILED"
            Output = $compileOutput
        }
        if ($StopOnFail) { break }
        continue
    }
    
    # Run with timeout (5 seconds)
    $job = Start-Job -ScriptBlock {
        param($exe)
        & $exe 2>&1
    } -ArgumentList $exePath
    
    $completed = Wait-Job $job -Timeout 5
    
    if (-not $completed) {
        Stop-Job $job
        Remove-Job $job -Force
        Write-Host "TIMEOUT (hang detected)" -ForegroundColor Red
        $script:failed++
        $script:results += [PSCustomObject]@{
            Test = $testName
            Status = "TIMEOUT"
            Output = "Process hung for 5+ seconds"
        }
        if ($StopOnFail) { break }
        continue
    }
    
    $output = Receive-Job $job
    Remove-Job $job
    
    # Filter out GC messages
    $filteredOutput = $output | Where-Object { $_ -notmatch '^\[GC\]' }
    
    # Check for PASS marker
    if ($filteredOutput -match "PASS: $testName") {
        Write-Host "PASS" -ForegroundColor Green
        $script:passed++
        $script:results += [PSCustomObject]@{
            Test = $testName
            Status = "PASS"
            Output = $filteredOutput
        }
    } else {
        Write-Host "FAIL (no PASS marker)" -ForegroundColor Red
        if ($Verbose) {
            Write-Host "Output:" -ForegroundColor DarkGray
            $filteredOutput | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
        }
        $script:failed++
        $script:results += [PSCustomObject]@{
            Test = $testName
            Status = "FAIL"
            Output = $filteredOutput
        }
        if ($StopOnFail) { break }
    }
}

# Summary
Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  Results Summary" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "Passed: $script:passed" -ForegroundColor Green
Write-Host "Failed: $script:failed" -ForegroundColor $(if ($script:failed -gt 0) { "Red" } else { "Green" })
Write-Host "Total:  $($script:passed + $script:failed)"

if ($script:failed -gt 0) {
    Write-Host ""
    Write-Host "Failed Tests:" -ForegroundColor Red
    $script:results | Where-Object { $_.Status -ne "PASS" } | ForEach-Object {
        Write-Host "  - $($_.Test): $($_.Status)" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "All pattern tests passed!" -ForegroundColor Green
exit 0
