# Express route parameters: /user/:id and multi-param routes

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/user/42",
        "expect_status": 200,
        "expect_json": {"id": "42"},
    },
    {
        "method": "GET",
        "path": "/user/alice",
        "expect_status": 200,
        "expect_json": {"id": "alice"},
    },
    {
        "method": "GET",
        "path": "/post/10/comment/99",
        "expect_status": 200,
        "expect_json": {"postId": "10", "commentId": "99"},
    },
]
