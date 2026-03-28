# Express HTTP method routing: GET, POST, PUT, DELETE, PATCH

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/item",
        "expect_status": 200,
        "expect_json": {"method": "GET"},
    },
    {
        "method": "POST",
        "path": "/item",
        "expect_status": 201,
        "expect_json": {"method": "POST"},
    },
    {
        "method": "PUT",
        "path": "/item",
        "expect_status": 200,
        "expect_json": {"method": "PUT"},
    },
    {
        "method": "DELETE",
        "path": "/item",
        "expect_status": 204,
    },
    {
        "method": "PATCH",
        "path": "/item",
        "expect_status": 200,
        "expect_json": {"method": "PATCH"},
    },
]
