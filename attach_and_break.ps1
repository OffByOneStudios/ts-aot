# Attach to hung process and get stack trace
# Usage: .\attach_and_break.ps1

$ErrorActionPreference = "Stop"

$cdbPath = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
if (-not (Test-Path $cdbPath)) {
    $cdbPath = "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe"
}

if (-not (Test-Path $cdbPath)) {
    Write-Host "CDB not found" -ForegroundColor Red
    exit 1
}

Write-Host "=== Attach and Break Debug ===" -ForegroundColor Cyan
Write-Host "Step 1: Starting test_require_simple.exe in background..." -ForegroundColor Yellow

# Start the process
$proc = Start-Process -FilePath "examples\test_require_simple.exe" -PassThru -WindowStyle Hidden

Write-Host "Process started (PID: $($proc.Id))" -ForegroundColor Green
Write-Host "Waiting 3 seconds for it to hang..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

if ($proc.HasExited) {
    Write-Host "Process already exited (exit code: $($proc.ExitCode))" -ForegroundColor Yellow
    exit 0
}

Write-Host "Step 2: Attaching CDB to hung process..." -ForegroundColor Cyan

# Create CDB script to break and get stack
$cdbScript = @'
.echo === Attached to hung process ===
~* k
.echo === Disassemble current location ===
u rip L20
.echo === Display registers ===
r
.echo === Done ===
q
'@

$cdbScript | Out-File -FilePath "attach_script.txt" -Encoding ASCII

# Attach CDB
$output = & $cdbPath -p $proc.Id -c '$$<attach_script.txt' 2>&1

Write-Host "`n=== CDB Output ===" -ForegroundColor Green
$output | ForEach-Object { Write-Host $_ }

# Kill the hung process
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue

# Save output
$output | Out-File -FilePath "hang_stack_trace.txt"
Write-Host "`nFull output saved to: hang_stack_trace.txt" -ForegroundColor Cyan

# Parse the stack trace
Write-Host "`n=== Analysis ===" -ForegroundColor Yellow
$stackLines = $output | Select-String -Pattern "test_require_simple!" | Select-Object -First 10
if ($stackLines) {
    Write-Host "Stack trace found:" -ForegroundColor Green
    $stackLines | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Host "No stack trace captured" -ForegroundColor Red
}

Remove-Item attach_script.txt -ErrorAction SilentlyContinue
