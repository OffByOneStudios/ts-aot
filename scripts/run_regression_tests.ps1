# Regression Test Script for Lodash Development
# Run this before and after each lodash fix to catch regressions

param(
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$script:failures = @()
$script:passes = @()

function Test-Executable {
    param([string]$Name, [string]$TsFile, [string]$ExpectedOutput)
    
    $exePath = $TsFile -replace '\.ts$', '.exe'
    
    Write-Host "Testing $Name... " -NoNewline
    
    # Compile
    $compileResult = & .\build\src\compiler\Release\ts-aot.exe $TsFile -o $exePath 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "COMPILE FAILED" -ForegroundColor Red
        if ($Verbose) { Write-Host $compileResult }
        $script:failures += "$Name (compile)"
        return
    }
    
    # Run (redirect stderr to stdout to avoid NativeCommandError)
    $output = cmd /c "$exePath 2>&1" | Where-Object { $_ -notmatch '^\[GC\]' }
    $exitCode = $LASTEXITCODE
    
    if ($exitCode -ne 0) {
        Write-Host "RUN FAILED (exit $exitCode)" -ForegroundColor Red
        if ($Verbose) { Write-Host $output }
        $script:failures += "$Name (run)"
        return
    }
    
    # Check output if expected
    if ($ExpectedOutput -and -not ($output -match $ExpectedOutput)) {
        Write-Host "OUTPUT MISMATCH" -ForegroundColor Yellow
        if ($Verbose) { 
            Write-Host "Expected: $ExpectedOutput"
            Write-Host "Got: $output"
        }
        $script:failures += "$Name (output)"
        return
    }
    
    Write-Host "PASS" -ForegroundColor Green
    $script:passes += $Name
}

Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  ts-aot Regression Test Suite" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

# Core functionality tests
Test-Executable "Inline Operations" "examples\test_inline_ops.ts" "All Tests Complete"
Test-Executable "Map/Set Operations" "examples\map_set_test.ts" "Set has '2' after clear"
Test-Executable "JSON Import (ES6)" "examples\json_import_test.ts" "JSON import test completed"
Test-Executable "JSON Require (CJS)" "examples\test_require_json.ts" "Done"
Test-Executable "JSON Require Full" "examples\test_require_json_full.ts" "All tests complete"
Test-Executable "Object Test" "examples\object_test.ts" ""

# Summary
Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  Results Summary" -ForegroundColor Cyan  
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "Passed: $($script:passes.Count)" -ForegroundColor Green
Write-Host "Failed: $($script:failures.Count)" -ForegroundColor $(if ($script:failures.Count -gt 0) { "Red" } else { "Green" })

if ($script:failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    foreach ($f in $script:failures) {
        Write-Host "  - $f" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "All tests passed!" -ForegroundColor Green
exit 0
