# Express Hello World - end-to-end server test
#
# Tests the Express example at examples/production/express-hello/index.ts:
# - Compiles and starts the server
# - Verifies GET / returns JSON with message and status
# - Verifies GET /health returns health check JSON

SERVER_SOURCE = "../../../examples/production/express-hello/index.ts"

# The server prints "Express listening on http://localhost:3000"
# Capture the port from the URL in case it changes
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/",
        "expect_status": 200,
        "expect_json": {"message": "Hello from ts-aot Express!", "status": "ok"},
        "expect_header": {"content-type": "application/json"},
    },
    {
        "method": "GET",
        "path": "/health",
        "expect_status": 200,
        "expect_body_contains": '"healthy":true',
    },
]
