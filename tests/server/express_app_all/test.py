# Express app.all(): single handler for all HTTP methods

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/any",
        "expect_status": 200,
        "expect_json": {"method": "GET"},
    },
    {
        "method": "POST",
        "path": "/any",
        "expect_status": 200,
        "expect_json": {"method": "POST"},
    },
    {
        "method": "PUT",
        "path": "/any",
        "expect_status": 200,
        "expect_json": {"method": "PUT"},
    },
    {
        "method": "DELETE",
        "path": "/any",
        "expect_status": 200,
        "expect_json": {"method": "DELETE"},
    },
]
