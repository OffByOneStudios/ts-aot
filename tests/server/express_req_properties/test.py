# Express request properties: req.path, req.hostname, req.protocol, req.ip

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/info",
        "expect_status": 200,
        "expect_body_contains": '"method":"GET"',
    },
    {
        # req.path requires defineGetter on prototype chain — currently null
        "method": "GET",
        "path": "/info",
        "expect_status": 200,
        "expect_body_contains": '"path":"/info"',
    },
    {
        # req.protocol requires defineGetter on prototype chain — currently undefined
        "method": "GET",
        "path": "/info",
        "expect_status": 200,
        "expect_body_contains": '"protocol":"http"',
    },
    {
        "method": "GET",
        "path": "/with-query?foo=bar",
        "expect_status": 200,
        "expect_body_contains": '"path":"/with-query"',
    },
]
