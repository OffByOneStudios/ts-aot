# Express app.route() — chainable route definition

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/resource",
        "expect_status": 200,
        "expect_json": {"method": "GET", "action": "list"},
    },
    {
        "method": "POST",
        "path": "/resource",
        "expect_status": 201,
        "expect_json": {"method": "POST", "action": "create"},
    },
    {
        "method": "PUT",
        "path": "/resource",
        "expect_status": 200,
        "expect_json": {"method": "PUT", "action": "update"},
    },
    {
        "method": "DELETE",
        "path": "/resource",
        "expect_status": 204,
    },
]
