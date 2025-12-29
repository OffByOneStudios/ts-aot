$serverProc = Start-Process -FilePath ".\tests\http_server_test.exe" -PassThru -NoNewWindow
Write-Host "Server started with PID $($serverProc.Id)"
Start-Sleep -Seconds 2

try {
    Write-Host "Sending request to http://localhost:8080/test-url..."
    $resp1 = Invoke-WebRequest -Uri "http://localhost:8080/test-url" -Method Get -TimeoutSec 5 -UseBasicParsing
    Write-Host "Response 1 Status: $($resp1.StatusCode)"
    Write-Host "Response 1 Content: $($resp1.Content)"

    Write-Host "Sending request to http://localhost:8080/json..."
    $resp2 = Invoke-WebRequest -Uri "http://localhost:8080/json" -Method Get -TimeoutSec 5 -UseBasicParsing
    Write-Host "Response 2 Status: $($resp2.StatusCode)"
    Write-Host "Response 2 Content: $($resp2.Content)"
} catch {
    Write-Host "Request failed: $_"
} finally {
    Write-Host "Stopping server..."
    Stop-Process -Id $serverProc.Id -Force
}
