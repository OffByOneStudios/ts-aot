# Express query string parsing via req.query

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/search?q=hello&limit=20",
        "expect_status": 200,
        "expect_json": {"q": "hello", "limit": "20"},
    },
    {
        "method": "GET",
        "path": "/search?q=test",
        "expect_status": 200,
        "expect_json": {"q": "test", "limit": "10"},
    },
    {
        "method": "GET",
        "path": "/empty",
        "expect_status": 200,
        "expect_json": {},
    },
]
