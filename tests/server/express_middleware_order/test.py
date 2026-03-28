# Express middleware execution order: three middlewares fire in sequence

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/",
        "expect_status": 200,
        "expect_json": {"tags": ["first", "second", "third"]},
    },
]
