$server = Start-Process -FilePath "tests\http_server_test.exe" -PassThru -NoNewWindow
Start-Sleep -Seconds 2

try {
    Write-Host "Running client..."
    .\tests\http_client_test.exe
} finally {
    Write-Host "Stopping server..."
    Stop-Process -Id $server.Id -Force
}
