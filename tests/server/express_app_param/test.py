# Express app.param() — route parameter preprocessing
# Known issue: parseInt("abc", 10) returns 0 instead of NaN in ts-aot

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/user/alice",
        "expect_status": 200,
        "expect_json": {"id": "alice", "processed": "ALICE"},
    },
    {
        "method": "GET",
        "path": "/item/42",
        "expect_status": 200,
        "expect_json": {"id": 42, "type": "number"},
    },
]
