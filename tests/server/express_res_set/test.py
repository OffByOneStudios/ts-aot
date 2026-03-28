# Express res.set() and res.type(): header convenience methods

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/set-header",
        "expect_status": 200,
        "expect_header": {
            "x-custom": "value1",
            "x-request-id": "abc-123",
        },
    },
    {
        "method": "GET",
        "path": "/type-json",
        "expect_status": 200,
        "expect_body_contains": '"typed":true',
    },
    {
        "method": "GET",
        "path": "/type-html",
        "expect_status": 200,
        "expect_body": "<p>hello</p>",
    },
    {
        "method": "GET",
        "path": "/type-text",
        "expect_status": 200,
        "expect_body": "plain text",
    },
]
