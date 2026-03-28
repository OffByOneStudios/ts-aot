# Express req.get() / req.header(): case-insensitive header access
# Known issue: req.get() crashes the server — likely because
# the Express request prototype (via defineGetter/Object.create) doesn't
# properly attach to native IncomingMessage objects.

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/echo",
        "headers": {"X-Custom": "hello123"},
        "expect_status": 200,
        "expect_body_contains": '"custom":"hello123"',
    },
    {
        "method": "GET",
        "path": "/raw-headers",
        "expect_status": 200,
        "expect_json": {"hasHost": True},
    },
]
