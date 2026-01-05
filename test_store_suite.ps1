# Test Store Bug - Systematic Testing
# Runs 5 progressively complex tests to isolate the variable store bug

Write-Host "`n=== Store Bug Test Suite ===`n" -ForegroundColor Cyan

$tests = @(
    @{N=1; Desc="Simple primitive variable"},
    @{N=2; Desc="Function call result"},
    @{N=3; Desc="Function pointer assignment"},
    @{N=4; Desc="Function pointer WITHOUT .call(this)"},
    @{N=5; Desc="Nested function return"}
)

$passed = 0
$failed = 0

foreach ($test in $tests) {
    $n = $test.N
    $desc = $test.Desc
    
    Write-Host "Test $n`: $desc" -ForegroundColor Yellow
    
    # Compile the test harness
    $tsFile = Get-ChildItem "examples\test_store_$n*.ts" | Select-Object -First 1
    if (-not $tsFile) {
        Write-Host "  Test file not found" -ForegroundColor Red
        $failed++
        continue
    }
    
    $compileOutput = .\build\src\compiler\Release\ts-aot.exe $tsFile.FullName -o "examples\test_store_$n.exe" 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ❌ COMPILATION FAILED" -ForegroundColor Red
        $failed++
        continue
    }
    
    # Run the test
    $output = .\examples\test_store_$n.exe 2>&1 | Out-String
    
    # Check for pass/fail
    if ($output -match "✅ PASS") {
        Write-Host "  ✅ PASS" -ForegroundColor Green
        $passed++
    } elseif ($output -match "❌ FAIL") {
        Write-Host "  ❌ FAIL" -ForegroundColor Red
        Write-Host "  Output: $($output -split "`n" | Select-Object -Last 3 -Join "`n  ")" -ForegroundColor Gray
        $failed++
        
        # Show IR-DEBUG logs if present
        $debugLogs = $output | Select-String -Pattern "IR-DEBUG|Storing|Loading" 
        if ($debugLogs) {
            Write-Host "  Debug logs:" -ForegroundColor Magenta
            $debugLogs | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
        }
    } else {
        Write-Host "  ⚠️  UNKNOWN (no pass/fail marker)" -ForegroundColor Yellow
        Write-Host "  Output: $($output -split "`n" | Select-Object -Last 3 -Join "`n  ")" -ForegroundColor Gray
        $failed++
    }
    
    Write-Host ""
}

Write-Host "`n=== Summary ===" -ForegroundColor Cyan
Write-Host "Passed: $passed / $($tests.Count)" -ForegroundColor $(if ($passed -eq $tests.Count) { "Green" } else { "Yellow" })
Write-Host "Failed: $failed / $($tests.Count)" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })

if ($failed -gt 0) {
    Write-Host ""
    Write-Host "First failure indicates where the bug appears" -ForegroundColor Yellow
    Write-Host 'Check IR with: .\build\src\compiler\Release\ts-aot.exe examples\test_store_N.js --dump-ir' -ForegroundColor Gray
}
