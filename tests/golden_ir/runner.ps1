#!/usr/bin/env pwsh
# Golden IR Test Runner - Validates LLVM IR output against CHECK patterns

param(
    [Parameter(Mandatory=$true)]
    [string]$TestPath,
    
    [switch]$UpdateGolden,
    [switch]$ShowDetails
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$CompilerPath = Join-Path $RootDir 'build\src\compiler\Release\ts-aot.exe'

# Test results
$script:TotalTests = 0
$script:PassedTests = 0
$script:FailedTests = 0
$script:FailureDetails = @()

function Test-CompilerExists {
    if (-not (Test-Path $CompilerPath)) {
        Write-Host 'ERROR: Compiler not found at' $CompilerPath -ForegroundColor Red
        Write-Host 'Run: cmake --build build --config Release' -ForegroundColor Yellow
        exit 1
    }
}

function Get-TestFiles {
    param([string]$Path)
    
    $FullPath = Join-Path $ScriptDir $Path
    
    if (Test-Path $FullPath -PathType Leaf) {
        return @($FullPath)
    }
    elseif (Test-Path $FullPath -PathType Container) {
        return Get-ChildItem -Path $FullPath -Recurse -Include *.ts,*.js | Select-Object -ExpandProperty FullName
    }
    else {
        Write-Host 'ERROR: Test path not found:' $Path -ForegroundColor Red
        exit 1
    }
}

function Parse-TestFile {
    param([string]$FilePath)
    
    $Lines = Get-Content $FilePath
    
    $Test = @{
        FilePath = $FilePath
        FileName = Split-Path $FilePath -Leaf
        RunCommand = $null
        CheckPatterns = @()
        ExpectedOutput = @()
        ExpectedExitCode = 0
    }
    
    $LineNum = 0
    foreach ($Line in $Lines) {
        $LineNum++
        
        if ($Line -match '^\s*//\s*RUN:\s*(.+)$') {
            $Test.RunCommand = $Matches[1].Trim()
        }
        elseif ($Line -match '^\s*//\s*CHECK(-NEXT|-NOT|-DAG)?:\s*(.+)$') {
            $CheckType = if ($Matches[1]) { $Matches[1].TrimStart('-') } else { 'CHECK' }
            $Pattern = $Matches[2].Trim()
            $Test.CheckPatterns += @{
                Type = $CheckType
                Pattern = $Pattern
                Line = $LineNum
            }
        }
        elseif ($Line -match '^\s*//\s*OUTPUT:\s*(.+)$') {
            $Test.ExpectedOutput += $Matches[1].Trim()
        }
        elseif ($Line -match '^\s*//\s*EXIT-CODE:\s*(\d+)$') {
            $Test.ExpectedExitCode = [int]$Matches[1]
        }
    }
    
    if (-not $Test.RunCommand) {
        $Test.RunCommand = 'ts-aot %s --dump-ir -o %t.exe && %t.exe'
    }
    
    return $Test
}

function Expand-RunCommand {
    param([string]$Command, [string]$TestFile, [string]$TempDir)
    
    $ExeName = [System.IO.Path]::GetFileNameWithoutExtension($TestFile)
    $ExePath = Join-Path $TempDir "$ExeName.exe"
    $IRPath = Join-Path $TempDir "$ExeName.ll"
    
    $Expanded = $Command `
        -replace '%ts-aot', $CompilerPath `
        -replace '%s', $TestFile `
        -replace '%t\.exe', $ExePath `
        -replace '%t\.ll', $IRPath `
        -replace '%t', (Join-Path $TempDir $ExeName)
    
    return @{
        Command = $Expanded
        ExePath = $ExePath
        IRPath = $IRPath
    }
}

function Invoke-TestCompilation {
    param([hashtable]$Test, [string]$TempDir)
    
    $Run = Expand-RunCommand -Command $Test.RunCommand -TestFile $Test.FilePath -TempDir $TempDir
    
    $Commands = $Run.Command -split '\s*&&\s*'
    
    $IROutput = ''
    $RuntimeOutput = ''
    $ExitCode = 0
    
    try {
        foreach ($Cmd in $Commands) {
            $Cmd = $Cmd.Trim()
            
            if ($Cmd -match '--dump-ir') {
                # Compilation with IR dump
                # Extract compiler path and all arguments
                $Parts = $Cmd -split '\s+', 2
                $CompilerExe = $Parts[0]
                $AllArgs = if ($Parts.Count -gt 1) { $Parts[1] } else { '' }
                
                if ($ShowDetails) {
                    Write-Host "  Running: $CompilerExe $AllArgs" -ForegroundColor Cyan
                }
                
                # Parse arguments properly
                $ArgList = $AllArgs -split '\s+' | Where-Object { $_ -ne '' }
                
                $IROutput = & $CompilerExe $ArgList 2>&1 | Out-String
                
                if ($LASTEXITCODE -ne 0) {
                    throw "Compilation failed with exit code $LASTEXITCODE"
                }
            }
            elseif ($Cmd -match '\.exe$') {
                if ($ShowDetails) {
                    Write-Host "  Running: $Cmd" -ForegroundColor Cyan
                }
                
                $RuntimeOutput = & cmd /c $Cmd 2>&1 | Out-String
                $ExitCode = $LASTEXITCODE
            }
        }
    }
    catch {
        return @{
            Success = $false
            Error = $_.Exception.Message
        }
    }
    
    return @{
        Success = $true
        IROutput = $IROutput
        RuntimeOutput = $RuntimeOutput.Trim()
        ExitCode = $ExitCode
    }
}

function Test-CheckPatterns {
    param([string]$IROutput, [array]$Patterns)
    
    $IRLines = $IROutput -split "`n" | ForEach-Object { $_.Trim() }
    $CurrentPos = 0
    $Failures = @()
    
    foreach ($Pattern in $Patterns) {
        $Type = $Pattern.Type
        $PatternStr = $Pattern.Pattern
        $Line = $Pattern.Line
        
        $Regex = $PatternStr `
            -replace '\{\{', '(?:' `
            -replace '\}\}', ')' `
            -replace '\.\*', '.*'
        
        switch ($Type) {
            'CHECK' {
                $Found = $false
                for ($i = $CurrentPos; $i -lt $IRLines.Count; $i++) {
                    if ($IRLines[$i] -match $Regex) {
                        $CurrentPos = $i + 1
                        $Found = $true
                        break
                    }
                }
                if (-not $Found) {
                    $Failures += "Line $Line : CHECK pattern not found: $PatternStr"
                }
            }
            
            'NOT' {
                for ($i = $CurrentPos; $i -lt $IRLines.Count; $i++) {
                    if ($IRLines[$i] -match $Regex) {
                        $Failures += "Line $Line : CHECK-NOT pattern should not appear: $PatternStr"
                        break
                    }
                }
            }
        }
    }
    
    return @{
        Success = ($Failures.Count -eq 0)
        Failures = $Failures
    }
}

function Test-Output {
    param([string]$Actual, $Expected, [int]$ExpectedExitCode, [int]$ActualExitCode)
    
    $Failures = @()
    
    if ($ActualExitCode -ne $ExpectedExitCode) {
        $Failures += "Exit code mismatch: expected $ExpectedExitCode, got $ActualExitCode"
    }
    
    if ($null -ne $Expected -and $Expected.Count -gt 0) {
        $ExpectedStr = ($Expected -join "`n").TrimEnd()
        $ActualStr = $Actual.TrimEnd("`r", "`n") -replace "`r`n", "`n" -replace "`r", "`n"
        
        if ($ActualStr -ne $ExpectedStr) {
            $Failures += "Output mismatch: expected '$ExpectedStr', got '$ActualStr'"
        }
    }
    
    return @{
        Success = ($Failures.Count -eq 0)
        Failures = $Failures
    }
}

function Invoke-GoldenTest {
    param([string]$TestFile)
    
    $script:TotalTests++
    
    $TestName = $TestFile.Replace($ScriptDir, '').TrimStart('\', '/')
    Write-Host "[$script:TotalTests] Testing: $TestName" -ForegroundColor Blue
    
    $Test = Parse-TestFile -FilePath $TestFile
    
    $TempDir = Join-Path $env:TEMP "golden_ir_$([System.Guid]::NewGuid())"
    New-Item -ItemType Directory -Path $TempDir | Out-Null
    
    try {
        $Result = Invoke-TestCompilation -Test $Test -TempDir $TempDir
        
        if (-not $Result.Success) {
            Write-Host '  X COMPILATION FAILED' -ForegroundColor Red
            Write-Host "  Error: $($Result.Error)" -ForegroundColor Red
            
            $script:FailedTests++
            $script:FailureDetails += @{
                Test = $TestName
                Stage = 'Compilation'
                Error = $Result.Error
            }
            return
        }
        
        if ($Test.CheckPatterns.Count -gt 0) {
            $CheckResult = Test-CheckPatterns -IROutput $Result.IROutput -Patterns $Test.CheckPatterns
            
            if (-not $CheckResult.Success) {
                Write-Host '  X CHECK PATTERNS FAILED' -ForegroundColor Red
                foreach ($Failure in $CheckResult.Failures) {
                    Write-Host "    $Failure" -ForegroundColor Red
                }
                
                $script:FailedTests++
                $script:FailureDetails += @{
                    Test = $TestName
                    Stage = 'CHECK Patterns'
                    Failures = $CheckResult.Failures
                }
                return
            }
        }
        
        if ($null -ne $Test.ExpectedOutput -and $Test.ExpectedOutput.Count -gt 0 -or $Test.ExpectedExitCode -ne 0) {
            $OutputResult = Test-Output `
                -Actual $Result.RuntimeOutput `
                -Expected $Test.ExpectedOutput `
                -ExpectedExitCode $Test.ExpectedExitCode `
                -ActualExitCode $Result.ExitCode
            
            if (-not $OutputResult.Success) {
                Write-Host '  X OUTPUT VALIDATION FAILED' -ForegroundColor Red
                foreach ($Failure in $OutputResult.Failures) {
                    Write-Host "    $Failure" -ForegroundColor Red
                }
                
                $script:FailedTests++
                $script:FailureDetails += @{
                    Test = $TestName
                    Stage = 'Output'
                    Failures = $OutputResult.Failures
                }
                return
            }
        }
        
        Write-Host '  + PASSED' -ForegroundColor Green
        $script:PassedTests++
    }
    finally {
        if (Test-Path $TempDir) {
            Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Show-Summary {
    Write-Host ''
    Write-Host ('=' * 60)
    Write-Host 'Golden IR Test Summary'
    Write-Host ('=' * 60)
    
    Write-Host "Total Tests: $script:TotalTests"
    Write-Host "Passed: $script:PassedTests" -ForegroundColor Green
    
    if ($script:FailedTests -gt 0) {
        Write-Host "Failed: $script:FailedTests" -ForegroundColor Red
    } else {
        Write-Host 'Failed: 0'
    }
    
    $PassRate = if ($script:TotalTests -gt 0) {
        [math]::Round(($script:PassedTests / $script:TotalTests) * 100, 1)
    } else { 0 }
    
    Write-Host "Pass Rate: $PassRate%" -ForegroundColor $(if ($PassRate -eq 100) { 'Green' } else { 'Yellow' })
    
    Write-Host ('=' * 60)
}

# Main execution
Test-CompilerExists

$TestFiles = Get-TestFiles -Path $TestPath

Write-Host ''
Write-Host 'Golden IR Test Runner' -ForegroundColor Cyan
Write-Host "Running $($TestFiles.Count) tests." -ForegroundColor Cyan
Write-Host ''

foreach ($TestFile in $TestFiles) {
    Invoke-GoldenTest -TestFile $TestFile
}

Show-Summary

exit $(if ($script:FailedTests -gt 0) { 1 } else { 0 })
