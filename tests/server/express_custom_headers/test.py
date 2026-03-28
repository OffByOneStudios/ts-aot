# Express custom response headers via res.setHeader()

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/",
        "expect_status": 200,
        "expect_body": "OK",
        "expect_header": {
            "x-custom-header": "custom-value",
            "x-request-id": "12345",
            "cache-control": "no-cache",
            "content-type": "text/plain",
        },
    },
]
