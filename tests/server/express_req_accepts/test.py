# Express req.accepts() / req.is() — content negotiation
# Known issue: accepts npm package can't read headers from native request object

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        # Default Accept: */* should match json first
        "method": "GET",
        "path": "/negotiate",
        "expect_status": 200,
        "expect_json": {"format": "json"},
    },
    {
        # req.is() with JSON content type
        "method": "POST",
        "path": "/check-type",
        "headers": {"Content-Type": "application/json"},
        "body": "{}",
        "expect_status": 200,
        "expect_json": {"isJson": True},
    },
]
