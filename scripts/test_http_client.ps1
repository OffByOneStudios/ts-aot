$server = Start-Process -FilePath "node" -ArgumentList "-e", "const http = require('http'); http.createServer((req, res) => { console.log('Server: Received request ' + req.method + ' ' + req.url); console.log('Server: Headers: ' + JSON.stringify(req.headers)); res.writeHead(200, {'Content-Type': 'text/plain'}); res.end('Hello from Node.js server!'); }).listen(8080, () => { console.log('Server: Listening on 8080'); });" -PassThru
Start-Sleep -Seconds 3

try {
    Write-Host "Compiling examples/http-client.ts..."
    & .\build\src\compiler\Release\ts-aot.exe examples/http-client.ts -o examples/http-client.exe
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }

    Write-Host "Running examples/http-client.exe..."
    & .\examples\http-client.exe
    Write-Host "Client finished with exit code $LASTEXITCODE"
} finally {
    Write-Host "Stopping server..."
    Stop-Process -Id $server.Id -Force
}
