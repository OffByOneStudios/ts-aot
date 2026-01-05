# Fix JavaScript tests to use module-level code instead of user_main()

$jsFiles = Get-ChildItem -Path "tests\golden_ir\javascript" -Recurse -Filter "*.js"

foreach ($file in $jsFiles) {
    Write-Host "Processing: $($file.Name)"
    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.UTF8Encoding]::new($false))
    
    # Replace @user_main with @__ts_module_init in CHECK lines
    $content = $content -replace '// CHECK: define \{\{.*\}\} @user_main', '// CHECK: define {{.*}} @__ts_module_init'
    
    # Remove XFAIL line if present
    $content = $content -replace '// XFAIL:.*\r?\n', ''
    
    # Extract the function body and move to top level
    if ($content -match 'function user_main\(\)(?::\s*\w+)?\s*\{([\s\S]*?)\n\}') {
        $functionBody = $matches[1].Trim()
        
        # Remove the return 0 statement if present
        $functionBody = $functionBody -replace '\s*return\s+0;\s*$', ''
        
        # Find where the function starts and replace with just the body
        $content = $content -replace 'function user_main\(\)(?::\s*\w+)?\s*\{[\s\S]*?\n\}', $functionBody
    }
    
    # Write back without BOM
    [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  Fixed: $($file.Name)"
}

Write-Host "`nAll JavaScript tests fixed!"
