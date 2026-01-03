# Debug Helper Script for ts-aot
# Usage: .\debug.ps1 examples\debug_test.exe

param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath,
    [switch]$BreakOnStart,
    [switch]$UseCDB
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

# Find CDB debugger
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

Write-Host ""
Write-Host "Available debuggers:" -ForegroundColor Cyan
if ($cdbPath) {
    Write-Host "   CDB (Console Debugger): $cdbPath" -ForegroundColor Green
} else {
    Write-Host "   CDB not found - install Windows SDK" -ForegroundColor DarkGray
}
Write-Host "   Visual Studio Debugger (GUI)" -ForegroundColor Green
Write-Host ""

# Handle CDB debugging
if ($UseCDB) {
    if (-not $cdbPath) {
        Write-Error "CDB not found. Install Windows SDK"
        exit 1
    }
    Write-Host "Starting CDB debugger..." -ForegroundColor Cyan
    Write-Host "Commands: g (go), k (stack), dv (variables), q (quit)" -ForegroundColor Yellow
    Write-Host "----------------------------------------" -ForegroundColor DarkGray
    & $cdbPath $ExePath
    exit 0
}

if ($BreakOnStart) {
    Write-Host "Opening in Visual Studio debugger..." -ForegroundColor Cyan
    $devenv = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
    & $devenv /debugexe $ExePath
    exit 0
}

Write-Host "Running executable (Ctrl+C to abort)..." -ForegroundColor Cyan
Write-Host "Tip: Use -UseCDB for command-line debugging or -BreakOnStart for GUI" -ForegroundColor DarkGray
Write-Host "----------------------------------------" -ForegroundColor DarkGray

try {
    $process = Start-Process -FilePath $ExePath -NoNewWindow -Wait -PassThru
    
    if ($process.ExitCode -ne 0) {
        Write-Host ""
        Write-Host "Process exited with code: $($process.ExitCode)" -ForegroundColor Red
        Write-Host ""
        Write-Host "To debug this crash:" -ForegroundColor Yellow
        if ($cdbPath) {
            Write-Host "  Command-line: .\debug.ps1 $ExePath -UseCDB" -ForegroundColor White
        }
        Write-Host "  GUI: .\debug.ps1 $ExePath -BreakOnStart" -ForegroundColor White
        exit $process.ExitCode
    } else {
        Write-Host ""
        Write-Host "Process completed successfully (exit code 0)" -ForegroundColor Green
    }
} catch {
    Write-Host ""
    Write-Host "Error running process: $_" -ForegroundColor Red
    exit 1
}
