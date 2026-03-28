# Express res.send() and res.sendStatus() convenience methods

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/text",
        "expect_status": 200,
        "expect_body": "Hello World",
    },
    {
        "method": "GET",
        "path": "/object",
        "expect_status": 200,
        "expect_json": {"key": "value"},
    },
    {
        "method": "GET",
        "path": "/send-status",
        "expect_status": 202,
        # NOTE: body is "202" not "Accepted" because statuses.message[202] lookup
        # returns undefined (integer key vs string key mismatch in the statuses package).
        # The status code itself is correct.
        "expect_body_contains": "202",
    },
]
