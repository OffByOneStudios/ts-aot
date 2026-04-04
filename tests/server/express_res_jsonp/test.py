# Express res.jsonp() — JSONP callback wrapping

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        # Without callback — should return plain JSON
        "method": "GET",
        "path": "/data",
        "expect_status": 200,
        "expect_json": {"value": 123},
    },
    {
        # With callback — should return wrapped JSONP
        "method": "GET",
        "path": "/wrapped?callback=myFunc",
        "expect_status": 200,
        "expect_body_contains": "myFunc(",
    },
]
