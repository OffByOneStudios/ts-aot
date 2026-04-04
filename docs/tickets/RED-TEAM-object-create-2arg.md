# RED-TEAM: Object.create 2-arg — bisected to single call

## Status: Root cause identified via bisection

## Bisection Result

Out of 46 `Object.create` 2-arg calls during Express module init, **call #45 is the sole cause of the hang.** Skipping only that call restores 22/23 server tests.

Call #45 is Express `express.js` line 51:
```js
app.response = Object.create(res, {
    app: { configurable: true, enumerable: true, writable: true, value: app }
})
```

Call #44 (the equivalent for `app.request`) does NOT cause hangs:
```js
app.request = Object.create(req, {
    app: { configurable: true, enumerable: true, writable: true, value: app }
})
```

## What's special about call #45

- The descriptor adds an `app` property whose value is the Express app **closure**
- The prototype is `res` (Express response module — `Object.create(http.ServerResponse.prototype)`)
- All attribute flags are `true` (enumerable, writable, configurable) — so enumerability is NOT the issue here
- The response prototype (`res`) has many methods (json, send, redirect, etc.) defined on it
- The request prototype (`req`) also has methods but call #44 doesn't hang

## Why response hangs but request doesn't

Express init middleware (`init.js:36`) does:
```js
setPrototypeOf(res, app.response)
```

After Fix #4, this sets the response prototype pointer. When a request arrives, Express creates a native `TsServerResponse` C++ object. `setPrototypeOf(res, app.response)` sets the side-map prototype to `app.response`.

When `app.response` now has an `app` own property (the closure), and Express middleware accesses `this.app` on the response, it finds the closure via the prototype chain. The closure is the Express app itself — a function with many properties including `settings`, `_router`, etc.

The hang likely occurs because:
1. Express's `res.json()` or `res.send()` accesses `this.app` to get settings
2. `this.app` returns the app closure via the prototype
3. Something in the Express response pipeline iterates over the response object's properties (or the app's properties) and encounters a cycle or recursive structure

## Investigation needed

The response-specific hang means the issue is in how `this.app` on a native C++ response object interacts with the Express response pipeline. Key questions:

1. Does `res.json()` access `this.app`? Check Express response.js for `this.app` usage.
2. When `this.app` is found via the side-map prototype (Fix #4), is it returning the correct closure, or a corrupted value?
3. Is there an infinite loop in the response method pipeline when `app` is accessible as an own property on the prototype?

## Files

| File | Line | What |
|------|------|------|
| `tests/npm/express/node_modules/express/lib/express.js` | 51 | The specific Object.create call |
| `tests/npm/express/node_modules/express/lib/middleware/init.js` | 36 | setPrototypeOf(res, app.response) |
| `tests/npm/express/node_modules/express/lib/response.js` | All `this.app` usages | How response accesses app |
| `src/runtime/src/TsObject.cpp` | `ts_object_get_property` | How native C++ objects resolve properties via side-map prototype chain |
