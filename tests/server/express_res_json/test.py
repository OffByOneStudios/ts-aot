# Express res.json() convenience method

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/json",
        "expect_status": 200,
        "expect_json": {"count": 42, "message": "hello"},
    },
    {
        "method": "GET",
        "path": "/status-json",
        "expect_status": 201,
        "expect_json": {"created": True},
    },
]
