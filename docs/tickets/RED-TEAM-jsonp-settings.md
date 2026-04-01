# RED-TEAM: express_res_jsonp — app.settings null in res.jsonp context

## Status: Open (partially fixed)

## Fixes Applied

### Fix 1: arguments object in monomorphized specializations (commit 84262df)
The specialization loop was missing `arguments` creation entirely. Fixed by adding hidden `__argN__` params, `ts_create_arguments_from_params` in prologues, and `ts_set_last_call_argc` before direct calls.

### Fix 2: LoadFunction closure identity (pending commit)
`LoadFunction` was creating a fresh TsClosure on every reference to a function declaration, breaking `a === b` identity and property sharing. Fixed by caching closures in module-level globals with lazy initialization.

## Remaining Issue

Despite both fixes, `express_res_jsonp` still fails. Debug shows `app.settings=null` and `app.get('jsonp callback name')` returns `undefined` inside `res.jsonp`.

### Why the identity fix doesn't help here

Express's `app` is a **function expression**, not a declaration:
```js
var app = function(req, res, next) { app.handle(req, res, next); };
```
Function expressions are correctly excluded from LoadFunction caching (they should create new closures per JS spec). The `app` variable is stored in an alloca and reused within `createApplication()` — so identity within that scope is preserved.

The `app` reference reaches `res.jsonp` via `this.app`, which was set up via:
```js
app.response = Object.create(res, {
    app: { configurable: true, enumerable: true, writable: true, value: app }
})
```

### Likely remaining root cause

The `this.app` lookup inside `res.jsonp` goes through `Object.create`'s prototype chain. The `app` property was set on the prototype via `Object.create(res, descriptors)`. The runtime's `Object.create` implementation may not properly store/retrieve the `app` closure through the prototype descriptor.

### Next investigation steps

1. Instrument `createApplication()` in `express.js` to verify `app.settings` is set after `app.init()`
2. Check if `app.response.app === app` holds after Object.create
3. Check if `this.app` in a response handler returns the same object as step 2
4. Verify `Object.create` with property descriptors preserves function values in the prototype

## Files to Investigate

| File | What to check |
|------|---------------|
| `src/runtime/src/TsObject.cpp` | `ts_object_create` — does Object.create with descriptors correctly store closure values? |
| `src/runtime/src/TsObject.cpp` | Prototype chain property lookup — does `this.app` traverse the prototype to find the descriptor value? |
