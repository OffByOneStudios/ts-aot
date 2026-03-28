# Express status code setting: 200, 201, 204, 400, 404, 500

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/ok",
        "expect_status": 200,
        "expect_body": "OK",
    },
    {
        "method": "GET",
        "path": "/created",
        "expect_status": 201,
        "expect_body": "Created",
    },
    {
        "method": "GET",
        "path": "/no-content",
        "expect_status": 204,
    },
    {
        "method": "GET",
        "path": "/bad-request",
        "expect_status": 400,
        "expect_body": "Bad Request",
    },
    {
        "method": "GET",
        "path": "/not-found",
        "expect_status": 404,
        "expect_body": "Not Found",
    },
    {
        "method": "GET",
        "path": "/server-error",
        "expect_status": 500,
        "expect_body": "Internal Server Error",
    },
]
