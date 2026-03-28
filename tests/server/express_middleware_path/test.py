# Express path-scoped middleware: app.use('/api', fn) only fires for /api/*

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/api/data",
        "expect_status": 200,
        "expect_json": {"isAPI": True},
    },
    {
        "method": "GET",
        "path": "/web",
        "expect_status": 200,
        "expect_json": {"isAPI": False},
    },
]
