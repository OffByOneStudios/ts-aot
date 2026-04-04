# Red Team: Express Feature Coverage Gaps

## Context

ts-aot compiles Express.js (v5-style, from npm) via the JS slow path. The server test suite has 17 tests, all passing. This ticket identifies Express features that real-world apps depend on but that have zero test coverage. The goal is to find features that silently produce wrong results or crash.

## Current Test Coverage (17/17 passing)

| Test | What it covers |
|------|---------------|
| express_hello | Basic GET route, manual JSON response |
| express_route_params | `/user/:id`, multi-params `/post/:pid/comment/:cid` |
| express_query_string | `req.query` parsing (?q=hello&limit=20) |
| express_methods | GET, POST, PUT, DELETE, PATCH routing |
| express_status_codes | 200, 201, 204, 400, 404, 500 via `res.status().send()` |
| express_middleware_order | Sequential middleware execution, `next()` |
| express_middleware_path | Path-scoped `app.use('/api', fn)` |
| express_custom_headers | `res.setHeader()`, `req.headers` access |
| express_res_json | `res.json()`, `res.status(201).json()` |
| express_res_send | `res.send(string)`, `res.send(object)`, `res.sendStatus()` |
| express_res_set | `res.set(field, val)`, `res.type()` |
| express_app_all | `app.all()` matches any HTTP method |
| express_404_fallthrough | Catch-all middleware for unmatched routes |
| express_error_middleware | `throw` and `next(err)` route to 4-arg error handler |
| express_redirect | `res.redirect('/new')`, `res.redirect(301, '/dest')` |
| express_req_headers | `req.get('Host')`, case-insensitive header lookup |
| express_req_properties | `req.path`, `req.protocol`, `req.hostname`, `req.ip` |

## Red Team Prompt

Use this prompt with another AI to identify gaps:

---

You are a senior backend engineer auditing an Express.js test suite for an AOT-compiled TypeScript runtime. The runtime compiles Express from source (JS slow path) and links it with native C++ implementations of Node.js APIs (http, net, fs, etc.).

The test suite currently covers: basic routing (GET/POST/PUT/DELETE/PATCH), route parameters, query strings, middleware ordering, path-scoped middleware, error middleware (throw + next(err)), res.json(), res.send(), res.sendStatus(), res.set(), res.type(), res.redirect(), req.get(), req.path, req.protocol, req.hostname, req.ip, req.query, req.params, req.headers, app.all(), and 404 fallthrough.

**Your task:** Identify Express features that are commonly used in production applications but are NOT covered by the tests above. For each feature, write a minimal server.ts and test.py that would expose a bug if the feature doesn't work.

Focus on features that:
1. Are used in >30% of real Express apps (based on your training data)
2. Exercise different code paths than the existing tests
3. Could silently return wrong values rather than crashing
4. Involve cross-module interactions (Express JS code calling native Node.js C++ methods)

### Format for each feature

```
## Feature: <name>
**Express method:** <e.g., res.cookie(name, val, opts)>
**Why it matters:** <1 sentence>
**What could go wrong in AOT:** <1 sentence about the risky code path>

### server.ts
<minimal Express server that uses this feature>

### test.py (pytest-style check dict)
CHECKS = [
    { "method": "GET", "path": "/...", "expect_status": 200, "expect_header": {...} },
]
```

### Priority categories

**Tier 1 - Common in real apps, high risk of silent failure:**
- res.cookie() / res.clearCookie() -- Sets Set-Cookie header via cross-module JS
- res.sendFile() -- Streams file via native fs, sets Content-Type via mime lookup
- res.download() -- Like sendFile but adds Content-Disposition header
- res.append() -- Appends to existing header (multi-value headers like Set-Cookie)
- res.jsonp() -- JSONP callback wrapping, reads query param
- req.accepts() / req.is() -- Content negotiation via Accepts/type-is npm packages
- app.param() -- Route parameter preprocessing callback
- app.route('/path').get(fn).post(fn) -- Chainable route definition

**Tier 2 - Edge cases that break apps silently:**
- res.format() with explicit Accept header -- Content type negotiation dispatch
- req.fresh / req.stale -- ETag/Last-Modified conditional request handling
- req.range() -- Range header parsing for partial content
- res.vary() -- Adds Vary header for cache correctness
- res.links() -- Sets Link header for pagination
- Mounted sub-apps: `app.use('/api', subApp)` where subApp = express()
- Router objects: `express.Router()` with isolated middleware stacks
- Multiple error handlers in sequence

**Tier 3 - Less common but architecturally interesting:**
- res.render() + app.engine() -- Template engine registration and rendering
- req.xhr -- X-Requested-With detection
- app.enable('trust proxy') / app.disable() -- Settings that affect req.ip, req.protocol
- req.originalUrl vs req.url after middleware rewriting
- res.locals propagation through middleware chain to final handler

### Specific cross-module risks to probe

These patterns are known to be fragile in the AOT compiler:

1. **Method chaining across modules:** `this.set(...).get(...)` where set/get are defined in Express's response.js but `this` is a native C++ TsServerResponse. The `return this;` in cross-module JS must correctly return the native object, not a wrapper.

2. **defineGetter properties:** Express uses `Object.defineProperty(req, 'path', { get: function() { return parse(this).pathname; } })` on the request prototype. These getters must resolve when accessed on native C++ TsIncomingMessage objects.

3. **arguments object:** Express's `res.redirect()` uses `arguments.length === 2` and `arguments[0]` for overloaded signatures. The AOT compiler's `arguments` implementation must work in cross-module JS functions.

4. **Prototype chain depth:** Express sets `res.__proto__ = app.response` which inherits from `http.ServerResponse.prototype`. Property lookups must traverse 3+ levels of prototype chain including native C++ objects.

5. **Callback identity:** Express uses `fn.length === 4` to detect error middleware. The AOT compiler must preserve `.length` on compiled functions.

### What NOT to test

- Body parsing (requires body-parser, not Express core)
- Cookie parsing (requires cookie-parser middleware)
- Session management (requires express-session)
- Static file serving via express.static (requires serve-static)
- WebSocket upgrade (not Express responsibility)

---

## Acceptance Criteria

- [ ] Other AI identifies at least 8 testable features not in current suite
- [ ] Each feature has a concrete server.ts + test.py
- [ ] Tests are compatible with the test runner format (CHECKS list, SERVER_SOURCE, READY_PATTERN)
- [ ] No overlap with existing 17 tests
