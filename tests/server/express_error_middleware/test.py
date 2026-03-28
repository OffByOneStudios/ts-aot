# Express error middleware: 4-arg (err, req, res, next) handler
# Known issue: throw in route handlers and next(err) don't route to
# error middleware — Express's default 404 fires instead.

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/ok",
        "expect_status": 200,
        "expect_json": {"ok": True},
    },
    {
        "method": "GET",
        "path": "/throw",
        "expect_status": 500,
        "expect_json": {"error": "something broke"},
    },
    {
        "method": "GET",
        "path": "/next-error",
        "expect_status": 500,
        "expect_json": {"error": "passed to next"},
    },
]
