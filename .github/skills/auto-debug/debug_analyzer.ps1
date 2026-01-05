# Automated Debug Analyzer for ts-aot
# Runs CDB debugger programmatically and captures crash information

param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath,
    [string]$OutputFile = "debug_analysis.txt"
)

$ErrorActionPreference = "Continue"

# Find CDB
$cdbPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe",
    "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe"
)
$cdbPath = $null
foreach ($path in $cdbPaths) {
    if (Test-Path $path) {
        $cdbPath = $path
        break
    }
}

if (-not $cdbPath) {
    Write-Error "CDB not found. Install Windows SDK."
    exit 1
}

if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
    exit 1
}

# Resolve to absolute path for CDB
$ExePath = (Resolve-Path $ExePath).Path

Write-Host "=== Automated Debug Analysis ===" -ForegroundColor Cyan
Write-Host "Executable: $ExePath" -ForegroundColor Yellow
Write-Host "Debugger: $cdbPath" -ForegroundColor Yellow
Write-Host ""

# CDB commands to execute - stop on first-chance access violation
$commands = @(
    ".sympath srv*https://msdl.microsoft.com/download/symbols"  # Symbol server
    ".reload"                                                    # Reload symbols
    "sxe -c `"kb 50;r;q`" av"                                   # On AV: print stack, regs, quit
    "g"                                                          # Run until crash/exit
    "!analyze -v"                                                # Automatic crash analysis
    "lm"                                                         # List modules
    "q"                                                          # Quit
)

# Create command script
$commandScript = ($commands -join "`n") + "`n"
$tempScript = [System.IO.Path]::GetTempFileName()
$commandScript | Out-File -FilePath $tempScript -Encoding ascii

Write-Host "Running debugger with automated commands..." -ForegroundColor Cyan
Write-Host ""

# Run CDB with command script using -cfr (command file then exit)
# Use -xe to set exception handling BEFORE running
$job = Start-Job -ScriptBlock {
    param($cdb, $script, $exe)
    & $cdb -xe av -cfr $script $exe 2>&1
} -ArgumentList $cdbPath, $tempScript, $ExePath

# Wait up to 30 seconds for the job to complete
$completed = Wait-Job $job -Timeout 30

if ($completed) {
    $output = Receive-Job $job
    Remove-Job $job
} else {
    Write-Host "WARNING: Debugger timed out after 30 seconds, stopping..." -ForegroundColor Yellow
    Stop-Job $job
    $output = Receive-Job $job
    Remove-Job $job
}

# Save full output
$output | Out-File -FilePath $OutputFile -Encoding utf8

# Clean up
Remove-Item $tempScript -ErrorAction SilentlyContinue

Write-Host "=== Debug Analysis Complete ===" -ForegroundColor Green
Write-Host "Full output saved to: $OutputFile" -ForegroundColor Yellow
Write-Host ""

# Parse and display key information
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host ""

$outputText = $output -join "`n"

# Extract exit code
if ($outputText -match "ExitProcess\((\d+)\)") {
    $exitCode = $matches[1]
    Write-Host "Exit Code: $exitCode" -ForegroundColor $(if ($exitCode -eq "0") { "Green" } else { "Red" })
}

# Extract exception info
if ($outputText -match "EXCEPTION_.*?(?:code|Code)\s+([0-9a-fA-F]+)") {
    Write-Host "Exception Code: 0x$($matches[1])" -ForegroundColor Red
}

# Extract call stack
if ($outputText -match "(?ms)Call Site.*?^(.+?)(?:\r?\n\r?\n|\z)") {
    Write-Host ""
    Write-Host "Call Stack (Top 10):" -ForegroundColor Yellow
    $stackLines = ($matches[1] -split "`n" | Select-Object -First 10)
    foreach ($line in $stackLines) {
        if ($line -match "ts_|user_main|add_") {
            Write-Host "  $line" -ForegroundColor White
        } else {
            Write-Host "  $line" -ForegroundColor DarkGray
        }
    }
}

# Extract source location if available
if ($outputText -match "(?i)Source\s+Line.*?:?\s*(\d+)") {
    Write-Host ""
    Write-Host "Source Line: $($matches[1])" -ForegroundColor Yellow
}

# Look for function names
if ($outputText -match "(?ms)Function.*?:\s*(.+?)(?:\r?\n|\z)") {
    Write-Host "Function: $($matches[1])" -ForegroundColor Yellow
}

# Extract any error messages
$errorPattern = "(?i)(error|exception|failure|assert|abort|crash).*"
$errors = [regex]::Matches($outputText, $errorPattern) | Select-Object -First 5
if ($errors.Count -gt 0) {
    Write-Host ""
    Write-Host "Error Messages:" -ForegroundColor Red
    foreach ($err in $errors) {
        Write-Host "  $($err.Value)" -ForegroundColor DarkRed
    }
}

Write-Host ""
Write-Host "For detailed analysis, see: $OutputFile" -ForegroundColor Cyan

# Return parsed data as JSON for programmatic access
$result = @{
    ExitCode = $exitCode
    HasCrash = $outputText -match "EXCEPTION"
    OutputFile = $OutputFile
    Summary = @{
        Exception = if ($outputText -match "EXCEPTION") { $true } else { $false }
        StackAvailable = $outputText -match "Call Site"
    }
}

return $result
