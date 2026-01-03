# Debug Helper Script for ts-aot
# Usage: .\debug.ps1 examples\debug_test.exe

param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath,
    [switch]$BreakOnStart,
    [string]$BreakOnFunction
)

$ErrorActionPreference = "Stop"

Write-Host "=== ts-aot Debug Helper ===" -ForegroundColor Cyan
Write-Host "Executable: $ExePath" -ForegroundColor Yellow

if (-not (Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
    exit 1
}

# Check if we have source .ts file
$tsFile = $ExePath -replace '\.exe$', '.ts'
if (Test-Path $tsFile) {
    Write-Host "Source file: $tsFile" -ForegroundColor Yellow
}

# Check if PDB exists
$pdbFile = $ExePath -replace '\.exe$', '.pdb'
if (Test-Path $pdbFile) {
    Write-Host "Debug symbols: $pdbFile" -ForegroundColor Green
} else {
    Write-Host "Warning: No PDB file found. Compile with -g flag." -ForegroundColor Red
}

Write-Host ""
Write-Host "Options:" -ForegroundColor Cyan
Write-Host "  1) Run normally (shows crash location if it crashes)"
Write-Host "  2) Open in Visual Studio debugger (GUI)"
Write-Host "  3) Install Windows SDK for CDB (command-line debugger)"
Write-Host ""

if ($BreakOnStart) {
    Write-Host "Opening in Visual Studio debugger with break on start..." -ForegroundColor Cyan
    $devenv = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
    & $devenv /debugexe $ExePath
    exit 0
}

Write-Host "Running executable (Ctrl+C to abort)..." -ForegroundColor Cyan
Write-Host "----------------------------------------" -ForegroundColor DarkGray

try {
    # Run with error handling to catch crashes
    $process = Start-Process -FilePath $ExePath -NoNewWindow -Wait -PassThru
    
    if ($process.ExitCode -ne 0) {
        Write-Host ""
        Write-Host "Process exited with code: $($process.ExitCode)" -ForegroundColor Red
        Write-Host ""
        Write-Host "To debug this crash:" -ForegroundColor Yellow
        Write-Host "  1. Run: .\debug.ps1 $ExePath -BreakOnStart" -ForegroundColor White
        Write-Host "  2. Or install Windows SDK and use: cdb.exe $ExePath" -ForegroundColor White
        exit $process.ExitCode
    } else {
        Write-Host ""
        Write-Host "Process completed successfully (exit code 0)" -ForegroundColor Green
    }
} catch {
    Write-Host ""
    Write-Host "Error running process: $_" -ForegroundColor Red
    Write-Host "To debug: .\debug.ps1 $ExePath -BreakOnStart" -ForegroundColor Yellow
    exit 1
}
