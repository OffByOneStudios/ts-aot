# Example: Copilot-Driven Debugging Loop
# This demonstrates how Copilot can use the auto-debug skill in a loop

Write-Host "=== Copilot Automated Debugging Example ===" -ForegroundColor Cyan
Write-Host ""

# Scenario: Debug a crashing executable
$exePath = "examples\debug_test.exe"

# Step 1: Check if executable exists
if (-not (Test-Path $exePath)) {
    Write-Host "[ERROR] Executable not found: $exePath" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Found executable: $exePath" -ForegroundColor Green

# Step 2: Run auto-debug analysis
Write-Host "`n[Running automated debug analysis...]" -ForegroundColor Cyan
$result = .\.github\skills\auto-debug\debug_analyzer.ps1 -ExePath $exePath

# Step 3: Analyze results
Write-Host "`n[Analysis Results]" -ForegroundColor Cyan

if ($result.HasCrash) {
    Write-Host "  [CRASH] CRASH DETECTED" -ForegroundColor Red
    Write-Host "  Exit Code: $($result.ExitCode)" -ForegroundColor Yellow
    Write-Host "  See: $($result.OutputFile)" -ForegroundColor Yellow
    
    # Step 4: Parse crash data for actionable insights
    $debugOutput = Get-Content $result.OutputFile -Raw
    
    # Look for ts-aot specific functions
    if ($debugOutput -match "user_main|add_int_int|module_init") {
        Write-Host "`n[Copilot Insights]" -ForegroundColor Cyan
        Write-Host "  - Crash occurred in compiled TypeScript code" -ForegroundColor White
        Write-Host "  - Check the function where crash happened" -ForegroundColor White
        Write-Host "  - Review debug symbols and source line mapping" -ForegroundColor White
    }
    
    # Look for common error patterns
    if ($debugOutput -match "Access violation|0xC0000005") {
        Write-Host "`n[Access Violation Detected]" -ForegroundColor Yellow
        Write-Host "  Likely causes:" -ForegroundColor White
        Write-Host "  - Null pointer dereference" -ForegroundColor Gray
        Write-Host "  - Buffer overflow" -ForegroundColor Gray
        Write-Host "  - Use-after-free" -ForegroundColor Gray
    }
    
    if ($debugOutput -match "Stack overflow|0xC00000FD") {
        Write-Host "`n[Stack Overflow Detected]" -ForegroundColor Yellow
        Write-Host "  Likely causes:" -ForegroundColor White
        Write-Host "  - Infinite recursion" -ForegroundColor Gray
        Write-Host "  - Large stack allocations" -ForegroundColor Gray
    }
    
} else {
    Write-Host "  [OK] No crash detected" -ForegroundColor Green
    if ($result.ExitCode -eq "0" -or $null -eq $result.ExitCode) {
        Write-Host "  [OK] Clean exit (code 0)" -ForegroundColor Green
    } else {
        Write-Host "  [WARNING] Non-zero exit code: $($result.ExitCode)" -ForegroundColor Yellow
    }
}

# Step 5: Extract actionable debugging suggestions
Write-Host "`n[Next Steps for Copilot]" -ForegroundColor Cyan

if ($result.HasCrash) {
    Write-Host "  1. Examine stack trace in $($result.OutputFile)" -ForegroundColor White
    Write-Host "  2. Identify the crashing function" -ForegroundColor White
    Write-Host "  3. Review source code at crash location" -ForegroundColor White
    Write-Host "  4. Check for null pointers or invalid memory access" -ForegroundColor White
    Write-Host "  5. Add runtime checks or assertions" -ForegroundColor White
} else {
    Write-Host "  1. Verify debug symbols are present (.pdb file)" -ForegroundColor White
    Write-Host "  2. Check application logs for warnings" -ForegroundColor White
    Write-Host "  3. Consider adding more instrumentation" -ForegroundColor White
}

Write-Host "`n[Copilot Capabilities]" -ForegroundColor Green
Write-Host "  - Parse the crash data automatically" -ForegroundColor Gray
Write-Host "  - Correlate with source code" -ForegroundColor Gray
Write-Host "  - Suggest fixes based on crash patterns" -ForegroundColor Gray
Write-Host "  - Iterate on multiple test cases" -ForegroundColor Gray

Write-Host ""
