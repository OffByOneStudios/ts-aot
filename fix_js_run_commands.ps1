# Fix RUN commands in JavaScript tests to use correct placeholder

$jsFiles = Get-ChildItem -Path "tests\golden_ir\javascript" -Recurse -Filter "*.js"

foreach ($file in $jsFiles) {
    Write-Host "Processing: $($file.Name)"
    
    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.UTF8Encoding]::new($false))
    
    # Replace RUN command placeholder
    # Old: // RUN: ts-aot %s --dump-ir -o %t.exe && %t.exe
    # New: // RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
    $content = $content -replace '// RUN: ts-aot ', '// RUN: %ts-aot '
    
    [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  Fixed: $($file.Name)"
}

Write-Host "`nAll JavaScript test RUN commands updated!"
