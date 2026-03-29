# Express res.redirect(): default 302 and explicit 301

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/old",
        "follow_redirects": False,
        "expect_status": 302,
        "expect_header": {"location": "/new"},
    },
    {
        "method": "GET",
        "path": "/moved",
        "follow_redirects": False,
        "expect_status": 301,
        "expect_header": {"location": "/destination"},
    },
    {
        # Verify redirect target is reachable
        "method": "GET",
        "path": "/new",
        "expect_status": 200,
        "expect_json": {"page": "new"},
    },
]
