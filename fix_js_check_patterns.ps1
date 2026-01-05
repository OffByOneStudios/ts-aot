# Fix CHECK patterns in JavaScript tests to match actual mangled function names

$jsFiles = Get-ChildItem -Path "tests\golden_ir\javascript" -Recurse -Filter "*.js"

foreach ($file in $jsFiles) {
    Write-Host "Processing: $($file.Name)"
    
    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.UTF8Encoding]::new($false))
    
    # Replace CHECK pattern for module init
    # Old: // CHECK: define {{.*}} @__ts_module_init
    # New: // CHECK: define {{.*}} @__module_init_{{.*}}_any
    $content = $content -replace '// CHECK: define \{\{.*\}\} @__ts_module_init', '// CHECK: define {{.*}} @__module_init_{{.*}}_any'
    
    [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.UTF8Encoding]::new($false))
    Write-Host "  Fixed: $($file.Name)"
}

Write-Host "`nAll JavaScript test CHECK patterns updated!"
