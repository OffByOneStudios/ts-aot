// Minimal QUnit shim for running lodash's test.js under ts-aot.
// Implements only the surface test.js uses:
//   QUnit.module/test/config/start/load/done
//   assert.expect/strictEqual/deepEqual/ok/notOk/async/notStrictEqual/notEqual/raises/equal
// Tallies pass/fail and prints a parseable summary line.

(function() {
  var root = (typeof global == 'object' && global) || this;

  var totalPass = 0;
  var totalFail = 0;
  var failMsgs = [];
  var queue = [];          // { module, name, fn }
  var curModule = '';
  var moduleHooks = {};    // module -> { beforeEach, afterEach }
  var pendingAsync = 0;
  var finished = false;
  var syncDone = false;   // true once the synchronous runAll pass completes

  function typeOf(v) {
    if (v === null) return 'null';
    if (Array.isArray(v)) return 'array';
    return typeof v;
  }

  // Structural deep-equality. Handles primitives, arrays, plain objects,
  // Date, RegExp, Map, Set, typed arrays, NaN, and circular refs.
  function deepEq(a, b, seen) {
    if (a === b) return true;
    // NaN
    if (a !== a && b !== b) return true;
    var ta = typeof a, tb = typeof b;
    if (ta != tb) return false;
    if (a == null || b == null) return a === b;
    if (ta != 'object') return false;

    seen = seen || [];
    for (var i = 0; i < seen.length; i++) {
      if (seen[i][0] === a && seen[i][1] === b) return true;
    }
    seen.push([a, b]);

    var sa = Object.prototype.toString.call(a);
    var sb = Object.prototype.toString.call(b);
    if (sa != sb) return false;

    if (sa == '[object Date]') return +a === +b;
    if (sa == '[object RegExp]') return String(a) === String(b);

    if (sa == '[object Map]') {
      if (a.size !== b.size) return false;
      var ok = true;
      a.forEach(function(val, key) {
        if (!b.has(key) || !deepEq(val, b.get(key), seen)) ok = false;
      });
      return ok;
    }
    if (sa == '[object Set]') {
      if (a.size !== b.size) return false;
      var arrA = [], arrB = [];
      a.forEach(function(v) { arrA.push(v); });
      b.forEach(function(v) { arrB.push(v); });
      return deepEq(arrA, arrB, seen);
    }

    // Arrays + typed arrays
    if (Array.isArray(a) || (typeof a.length == 'number' && sa.indexOf('Array]') >= 0)) {
      if (a.length !== b.length) return false;
      for (var j = 0; j < a.length; j++) {
        if (!deepEq(a[j], b[j], seen)) return false;
      }
      return true;
    }

    // Plain objects
    var keysA = Object.keys(a), keysB = Object.keys(b);
    if (keysA.length !== keysB.length) return false;
    keysA.sort(); keysB.sort();
    for (var k = 0; k < keysA.length; k++) {
      if (keysA[k] !== keysB[k]) return false;
      if (!deepEq(a[keysA[k]], b[keysB[k]], seen)) return false;
    }
    return true;
  }

  // Safe stringify — never throws on Symbol/object-with-bad-toString.
  function safe(v) {
    try {
      if (typeof v === 'symbol') return v.toString();
      return String(v);
    } catch (e) { return '<unstringifiable ' + (typeof v) + '>'; }
  }

  function record(pass, msg) {
    if (pass) { totalPass++; }
    else {
      totalFail++;
      if (failMsgs.length < 60) failMsgs.push(msg || '(no message)');
    }
  }

  function makeAssert() {
    return {
      expect: function() {},
      strictEqual: function(a, b, m) { record(a === b, safe(m) + ' :: expected ' + safe(b) + ' got ' + safe(a)); },
      notStrictEqual: function(a, b, m) { record(a !== b, safe(m)); },
      equal: function(a, b, m) { record(a == b, safe(m) + ' :: expected ' + safe(b) + ' got ' + safe(a)); },
      notEqual: function(a, b, m) { record(a != b, safe(m)); },
      deepEqual: function(a, b, m) { record(deepEq(a, b), safe(m) + ' :: deepEqual mismatch'); },
      notDeepEqual: function(a, b, m) { record(!deepEq(a, b), safe(m)); },
      ok: function(v, m) { record(!!v, safe(m) + ' :: expected truthy got ' + safe(v)); },
      notOk: function(v, m) { record(!v, safe(m) + ' :: expected falsy got ' + safe(v)); },
      raises: function(fn, expected, m) {
        var threw = false;
        try { fn(); } catch (e) { threw = true; }
        record(threw, m + ' :: expected throw');
      },
      async: function() {
        pendingAsync++;
        var called = false;
        return function() {
          if (called) return;
          called = true;
          pendingAsync--;
          // Only finalize once the synchronous registration/run pass is
          // complete — otherwise an early async test that resolves
          // synchronously would print the tally mid-suite and latch.
          if (syncDone && pendingAsync === 0) maybeFinish();
        };
      }
    };
  }
  // throws is an alias of raises
  // (assigned per-assert below via prototype-less object; add here)

  function runAll() {
    for (var i = 0; i < queue.length; i++) {
      var t = queue[i];
      var hooks = moduleHooks[t.module] || {};
      var assert = makeAssert();
      assert.throws = assert.raises;
      try {
        if (hooks.beforeEach) hooks.beforeEach.call(t.ctx, assert);
        t.fn.call(t.ctx, assert);
        if (hooks.afterEach) hooks.afterEach.call(t.ctx, assert);
      } catch (e) {
        record(false, '[' + t.module + '] ' + t.name + ' THREW: ' + (e && e.message ? e.message : e));
      }
    }
    syncDone = true;
    maybeFinish();
  }

  function maybeFinish() {
    if (finished) return;
    if (!syncDone) return;
    if (pendingAsync > 0) return;
    finished = true;
    console.log('----------------------------------------');
    console.log('LODASH-QUNIT PASS: ' + totalPass + '  FAIL: ' + totalFail + '  TOTAL: ' + (totalPass + totalFail));
    if (failMsgs.length) {
      console.log('--- first failures ---');
      for (var i = 0; i < failMsgs.length; i++) console.log('  ' + failMsgs[i]);
    }
    console.log('----------------------------------------');
  }

  var QUnit = {
    config: { autostart: false },
    module: function(name, hooks) {
      curModule = name;
      if (hooks && (hooks.beforeEach || hooks.afterEach)) moduleHooks[name] = hooks;
    },
    test: function(name, fn) {
      queue.push({ module: curModule, name: name, fn: fn, ctx: {} });
    },
    start: function() {},
    load: function() {},
    done: function() {},
    // Harness entrypoint:
    __run: runAll
  };

  root.QUnit = QUnit;
})();
