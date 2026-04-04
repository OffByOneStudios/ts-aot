# Express res.append() — multi-value header accumulation
# Known issue: setHeader with array value serializes as [object Array]

SERVER_SOURCE = "server.ts"
READY_PATTERN = r"listening on http://localhost:(\d+)"
READY_TIMEOUT = 15

CHECKS = [
    {
        # Vary header works (single-value appends work via set)
        "method": "GET",
        "path": "/vary",
        "expect_status": 200,
        "expect_json": {"ok": True},
    },
    {
        # Multi-value header — tests array serialization
        "method": "GET",
        "path": "/multi",
        "expect_status": 200,
        "expect_json": {"ok": True},
        "expect_header": {"x-custom": "one, two"},
    },
]
