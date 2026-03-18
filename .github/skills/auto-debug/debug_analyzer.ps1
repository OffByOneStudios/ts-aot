param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath,

    [string]$OutputFile = "debug_analysis.txt",

    [string]$Arguments = ""
)

# Resolve paths
$ExePath = Resolve-Path $ExePath -ErrorAction Stop
$PdbPath = [System.IO.Path]::ChangeExtension($ExePath, ".pdb")

Write-Host "=== ts-aot Auto-Debug Analyzer ===" -ForegroundColor Cyan
Write-Host "Executable: $ExePath"
Write-Host "PDB exists: $(Test-Path $PdbPath)"
Write-Host ""

# Build CDB command script
$cdbScript = @"
.symopt+0x10
.lines -e
sxn av
g
.echo ===EXCEPTION_INFO===
r
.echo ===STACK_TRACE===
kn 50
.echo ===SOURCE_LINES===
lsa .
.echo ===ANALYZE===
!analyze -v
.echo ===END===
q
"@

$scriptFile = [System.IO.Path]::GetTempFileName()
$cdbScript | Out-File -FilePath $scriptFile -Encoding ASCII

try {
    # Run CDB
    $cdbArgs = @(
        "-g",           # Go on start
        "-G",           # Go on exit
        "-y", "`"$([System.IO.Path]::GetDirectoryName($ExePath))`"",  # Symbol path
        "-srcpath", "`"$PWD`"",  # Source path
        "-cf", $scriptFile,
        $ExePath
    )

    if ($Arguments) {
        $cdbArgs += $Arguments
    }

    Write-Host "Running CDB..." -ForegroundColor Yellow
    $output = & cdb @cdbArgs 2>&1 | Out-String

    # Save full output
    $output | Out-File -FilePath $OutputFile -Encoding UTF8
    Write-Host "Full output saved to: $OutputFile" -ForegroundColor Green

    # Parse and display summary
    Write-Host ""
    Write-Host "=== CRASH SUMMARY ===" -ForegroundColor Red

    # Extract exception info
    if ($output -match "ExceptionCode:\s*(0x[0-9a-fA-F]+)") {
        $code = $Matches[1]
        $desc = switch ($code) {
            "0xc0000005" { "Access Violation" }
            "0xc0000094" { "Integer Divide by Zero" }
            "0xc00000fd" { "Stack Overflow" }
            "0xe06d7363" { "C++ Exception" }
            default { "Unknown" }
        }
        Write-Host "Exception: $code ($desc)" -ForegroundColor Yellow
    }

    # Extract faulting address
    if ($output -match "ExceptionAddress:\s*(0x[0-9a-fA-F]+)") {
        Write-Host "Fault Address: $($Matches[1])"
    }

    # Extract stack trace section
    $stackStart = $output.IndexOf("===STACK_TRACE===")
    $stackEnd = $output.IndexOf("===SOURCE_LINES===")
    if ($stackStart -ge 0 -and $stackEnd -ge 0) {
        $stack = $output.Substring($stackStart + 18, $stackEnd - $stackStart - 18).Trim()
        Write-Host ""
        Write-Host "=== STACK TRACE ===" -ForegroundColor Cyan
        # Show first 15 frames
        $lines = $stack -split "`n" | Select-Object -First 15
        foreach ($line in $lines) {
            if ($line -match "!") {
                Write-Host $line -ForegroundColor White
            } else {
                Write-Host $line
            }
        }
    }

    # Extract source lines
    $srcStart = $output.IndexOf("===SOURCE_LINES===")
    $srcEnd = $output.IndexOf("===LOCALS===")
    if ($srcStart -ge 0 -and $srcEnd -ge 0) {
        $src = $output.Substring($srcStart + 19, $srcEnd - $srcStart - 19).Trim()
        if ($src -and $src.Length -gt 5) {
            Write-Host ""
            Write-Host "=== SOURCE CONTEXT ===" -ForegroundColor Cyan
            Write-Host $src
        }
    }

    # Check for normal exit
    if ($output -match "Process exited") {
        Write-Host ""
        Write-Host "Process exited normally (no crash)" -ForegroundColor Green
    }

} finally {
    Remove-Item $scriptFile -ErrorAction SilentlyContinue
}
