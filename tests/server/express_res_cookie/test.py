# Express res.cookie() / res.clearCookie()
# Known issue: cookie npm package rejects "encode" option — crashes with 500

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        "method": "GET",
        "path": "/set",
        "expect_status": 200,
        "expect_json": {"ok": True},
        "expect_header": {"set-cookie": "name=value; Path=/"},
    },
    {
        "method": "GET",
        "path": "/set-opts",
        "expect_status": 200,
        "expect_json": {"ok": True},
    },
]
