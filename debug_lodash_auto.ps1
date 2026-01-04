# CDB Automated Debugging Script for Lodash Hang
# Usage: .\debug_lodash_auto.ps1

$ErrorActionPreference = "Stop"

$cdbPath = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
if (-not (Test-Path $cdbPath)) {
    $cdbPath = "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe"
}

if (-not (Test-Path $cdbPath)) {
    Write-Host "CDB not found. Install Windows SDK" -ForegroundColor Red
    exit 1
}

Write-Host "=== Automated CDB Debugging ===" -ForegroundColor Cyan
Write-Host "Executable: examples\test_require_simple.exe" -ForegroundColor Yellow
Write-Host "Strategy: Run for 5 seconds, then break and show stack" -ForegroundColor Yellow
Write-Host ""

# Run program in background with CDB
$process = Start-Process -FilePath $cdbPath `
    -ArgumentList "-g examples\test_require_simple.exe" `
    -NoNewWindow `
    -PassThru `
    -RedirectStandardOutput "cdb_output.txt" `
    -RedirectStandardError "cdb_error.txt"

Write-Host "Process started (PID: $($process.Id))" -ForegroundColor Green
Write-Host "Waiting 5 seconds for hang..." -ForegroundColor Yellow

Start-Sleep -Seconds 5

if (-not $process.HasExited) {
    Write-Host "Process still running - likely hung!" -ForegroundColor Red
    Write-Host "Breaking into process to get stack trace..." -ForegroundColor Cyan
    
    # Kill the process
    Stop-Process -Id $process.Id -Force
    
    Write-Host "`nProgram hung. Output:" -ForegroundColor Yellow
    if (Test-Path "cdb_output.txt") {
        Get-Content "cdb_output.txt" | Select-Object -Last 50
    }
} else {
    Write-Host "Process completed! Exit code: $($process.ExitCode)" -ForegroundColor Green
    if (Test-Path "cdb_output.txt") {
        Get-Content "cdb_output.txt"
    }
}

# Cleanup
Remove-Item cdb_output.txt -ErrorAction SilentlyContinue
Remove-Item cdb_error.txt -ErrorAction SilentlyContinue
