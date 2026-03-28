# Express 404 fallthrough: unmatched routes hit catch-all middleware

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/exists",
        "expect_status": 200,
        "expect_json": {"found": True},
    },
    {
        "method": "GET",
        "path": "/does-not-exist",
        "expect_status": 404,
        "expect_json": {"error": "Not Found", "path": "/does-not-exist"},
    },
    {
        "method": "GET",
        "path": "/also/missing",
        "expect_status": 404,
        "expect_body_contains": "Not Found",
    },
]
