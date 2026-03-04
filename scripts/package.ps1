# package.ps1 - Create a portable ts-aot distribution zip
# Usage: .\scripts\package.ps1 [-OutputDir dist] [-NoPdb] [-BundleIcu]
#
# Creates: ts-aot-win-x64.zip containing everything needed to compile
# TypeScript to native executables. Unzip, add to PATH, done.

param(
    [string]$OutputDir = "dist",
    [switch]$NoPdb,
    [switch]$BundleIcu
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path "$RepoRoot\src\compiler")) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

$BuildDir = "$RepoRoot\build"
$CompilerRelease = "$BuildDir\src\compiler\Release"
$RuntimeRelease = "$BuildDir\src\runtime\Release"
$ExtensionsDir = "$RepoRoot\extensions\node"
$VcpkgLib = "$RepoRoot\vcpkg_installed\x64-windows-static-md\lib"

# Verify build exists
if (-not (Test-Path "$CompilerRelease\ts-aot.exe")) {
    Write-Error "ts-aot.exe not found. Run 'cmake --build build --config Release' first."
    exit 1
}

# Create staging directory
$StageDir = "$OutputDir\ts-aot"
if (Test-Path $StageDir) {
    Remove-Item -Recurse -Force $StageDir
}
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\lib" | Out-Null
New-Item -ItemType Directory -Force -Path "$StageDir\extensions\node" | Out-Null

Write-Host "Packaging ts-aot distribution..." -ForegroundColor Cyan

# 1. Compiler executable
Write-Host "  Copying compiler..."
Copy-Item "$CompilerRelease\ts-aot.exe" "$StageDir\"

# 2. ICU data file
$IcuDat = "$CompilerRelease\icudt74l.dat"
if (Test-Path $IcuDat) {
    Write-Host "  Copying ICU data..."
    Copy-Item $IcuDat "$StageDir\"
} else {
    Write-Warning "icudt74l.dat not found at $IcuDat"
}

# 3. Debug symbols (optional)
if (-not $NoPdb) {
    $Pdb = "$CompilerRelease\ts-aot.pdb"
    if (Test-Path $Pdb) {
        Write-Host "  Copying debug symbols..."
        Copy-Item $Pdb "$StageDir\"
    }
}

# 4. Runtime libraries (tsruntime + icudt_stub)
Write-Host "  Copying runtime libraries..."
Get-ChildItem "$RuntimeRelease\*.lib" | ForEach-Object {
    Copy-Item $_.FullName "$StageDir\lib\"
}

# 5. Extension libraries
Write-Host "  Copying extension libraries..."
$ExtModules = @(
    "core", "path", "os", "util", "assert", "url", "dns", "dgram",
    "zlib", "crypto", "events", "stream", "fs", "tty", "net", "vm",
    "v8", "inspector", "module", "readline", "string_decoder",
    "perf_hooks", "async_hooks", "http", "http2", "child_process",
    "cluster", "fetch"
)

foreach ($mod in $ExtModules) {
    $libDir = "$BuildDir\extensions\node\$mod\Release"
    if (Test-Path $libDir) {
        Get-ChildItem "$libDir\*.lib" | ForEach-Object {
            Copy-Item $_.FullName "$StageDir\lib\"
        }
    } else {
        Write-Warning "Extension lib not found: $mod"
    }
}

# 6. vcpkg dependency libraries
Write-Host "  Copying vcpkg dependencies..."
$VcpkgLibs = @(
    "tommath.lib", "spdlog.lib", "fmt.lib", "libuv.lib",
    "icuuc.lib", "icuin.lib",
    "libsodium.lib", "llhttp.lib", "libssl.lib", "libcrypto.lib",
    "cares.lib", "nghttp2.lib", "zlib.lib",
    "brotlicommon.lib", "brotlidec.lib", "brotlienc.lib"
)

if ($BundleIcu) {
    $VcpkgLibs += "icudt.lib"
}

foreach ($lib in $VcpkgLibs) {
    $src = "$VcpkgLib\$lib"
    if (Test-Path $src) {
        Copy-Item $src "$StageDir\lib\"
    } else {
        Write-Warning "vcpkg lib not found: $lib"
    }
}

# 7. Extension contracts (.ext.json)
Write-Host "  Copying extension contracts..."
Get-ChildItem -Path $ExtensionsDir -Directory | ForEach-Object {
    $modName = $_.Name
    $extJson = Get-ChildItem "$($_.FullName)\*.ext.json" -ErrorAction SilentlyContinue
    if ($extJson) {
        $destDir = "$StageDir\extensions\node\$modName"
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        $extJson | ForEach-Object {
            Copy-Item $_.FullName $destDir
        }
    }
}

# 8. Create zip
$ZipPath = "$OutputDir\ts-aot-win-x64.zip"
if (Test-Path $ZipPath) {
    Remove-Item $ZipPath
}

Write-Host "  Creating zip archive..."
Compress-Archive -Path $StageDir -DestinationPath $ZipPath

# Summary
$zipSize = (Get-Item $ZipPath).Length / 1MB
$fileCount = (Get-ChildItem -Recurse $StageDir -File).Count

Write-Host ""
Write-Host "Distribution created:" -ForegroundColor Green
Write-Host "  Archive: $ZipPath"
Write-Host "  Size:    $([math]::Round($zipSize, 1)) MB"
Write-Host "  Files:   $fileCount"
Write-Host ""
Write-Host "To use:" -ForegroundColor Yellow
Write-Host "  1. Unzip to a directory (e.g., C:\tools\ts-aot)"
Write-Host "  2. Add ts-aot\ to your PATH"
Write-Host "  3. Run: ts-aot hello.ts -o hello.exe"

# Cleanup staging directory
Remove-Item -Recurse -Force $StageDir
