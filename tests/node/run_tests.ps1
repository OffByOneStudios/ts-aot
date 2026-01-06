# Node.js API Test Runner (PowerShell)
# Compiles and runs all Node.js API tests

$ErrorActionPreference = "Stop"

# Find script directory and project root
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$CompilerPath = Join-Path $RootDir "build\src\compiler\Release\ts-aot.exe"

# Check if compiler exists
if (-not (Test-Path $CompilerPath)) {
    Write-Host "ERROR: Compiler not found at $CompilerPath" -ForegroundColor Red
    Write-Host "Please build the compiler first."
    exit 1
}

# Initialize counters
$TotalTests = 0
$PassedTests = 0
$FailedTests = 0
$BlockedTests = 0
$SkippedTests = 0
$CompileErrors = 0

Write-Host ""
Write-Host "=== Node.js API Test Suite ===" -ForegroundColor Cyan
Write-Host ""

# Find all test files (excluding test_template.ts)
$TestFiles = Get-ChildItem -Path $ScriptDir -Recurse -Filter "*.ts" | Where-Object {
    $_.Name -ne "test_template.ts" -and $_.DirectoryName -notmatch "\.build"
}

Write-Host "Found $($TestFiles.Count) test file(s)"
Write-Host ""

foreach ($TestFile in $TestFiles) {
    $TestName = "$($TestFile.Directory.Name)/$($TestFile.Name)"
    $ExePath = $TestFile.FullName -replace '\.ts$', '.exe'

    $TotalTests++

    # Compile
    try {
        $CompileOutput = & $CompilerPath $TestFile.FullName -o $ExePath 2>&1
        $CompileExitCode = $LASTEXITCODE

        if ($CompileExitCode -ne 0) {
            Write-Host "  COMPILE ERR  $TestName" -ForegroundColor Red
            $CompileErrors++
            $FailedTests++
            continue
        }
    } catch {
        Write-Host "  COMPILE ERR  $TestName" -ForegroundColor Red
        Write-Host "    Exception: $_" -ForegroundColor Red
        $CompileErrors++
        $FailedTests++
        continue
    }

    # Run test
    try {
        $RunOutput = & $ExePath 2>&1 | Out-String
        $RunExitCode = $LASTEXITCODE

        # Check if blocked
        $IsBlocked = $RunOutput -match "blocked pending|blocked by"

        # Check if skipped
        $IsSkipped = $RunOutput -match "SKIP:"

        # Clean up executable
        if (Test-Path $ExePath) {
            Remove-Item $ExePath -Force -ErrorAction SilentlyContinue
        }

        # Determine status
        if ($IsBlocked) {
            Write-Host "  BLOCKED      $TestName" -ForegroundColor Yellow
            $BlockedTests++
        } elseif ($IsSkipped) {
            Write-Host "  SKIPPED      $TestName" -ForegroundColor Cyan
            $SkippedTests++
        } elseif ($RunExitCode -eq 0) {
            Write-Host "  PASS         $TestName" -ForegroundColor Green
            $PassedTests++
        } else {
            Write-Host "  FAIL         $TestName" -ForegroundColor Red
            # Show last few lines of output
            $Lines = $RunOutput -split "`n"
            $Lines[-5..-1] | ForEach-Object {
                Write-Host "    $_" -ForegroundColor Red
            }
            $FailedTests++
        }
    } catch {
        Write-Host "  FAIL         $TestName" -ForegroundColor Red
        Write-Host "    Exception: $_" -ForegroundColor Red
        $FailedTests++

        # Clean up executable
        if (Test-Path $ExePath) {
            Remove-Item $ExePath -Force -ErrorAction SilentlyContinue
        }
    }
}

# Print summary
Write-Host ""
Write-Host "=== Test Summary ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "Total tests:       $TotalTests"
Write-Host "Passed:            $PassedTests" -ForegroundColor Green
Write-Host "Failed:            $FailedTests" -ForegroundColor Red
Write-Host "Blocked:           $BlockedTests" -ForegroundColor Yellow
Write-Host "Skipped:           $SkippedTests" -ForegroundColor Cyan
Write-Host "Compile errors:    $CompileErrors" -ForegroundColor Red

if ($PassedTests -gt 0) {
    $RunableTests = $TotalTests - $BlockedTests - $SkippedTests
    if ($RunableTests -gt 0) {
        $PassRate = [math]::Round(($PassedTests / $RunableTests) * 100, 1)
        Write-Host ""
        Write-Host "Pass rate (excluding blocked/skipped): $PassRate%"
    }
}

# Exit code
if ($FailedTests -gt 0 -or $CompileErrors -gt 0) {
    exit 1
} else {
    exit 0
}
