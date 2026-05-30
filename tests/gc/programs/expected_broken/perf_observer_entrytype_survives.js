// A PerformanceObserver's observed entryType string is held only in the
// observer's entryTypes_ vector (a malloc-backed std::vector inside the C++
// object). If the GC doesn't scan that vector, the string is swept while the
// observer is still alive -> use-after-free in NotifyObservers' strcmp.
function user_main() {
  var ph = require('perf_hooks');
  var PerformanceObserver = ph.PerformanceObserver;
  var obs = new PerformanceObserver(function () {});
  // long, unique, NON-interned entry type (short/common strings are interned
  // and rooted via globalStringCache, which would mask the bug)
  var t = 'customtype_' + [1, 2, 3, 4, 5, 6, 7, 8, 9].join('x');
  obs.observe({ entryTypes: [t] });
  __ts_gc_watch(t);     // track this exact string
  t = null;             // only reference now is the observer's entryTypes_
  __ts_gc_major();      // force a full collection
  if (!__ts_gc_watch_alive()) { console.log("FAIL: entryTypes_ string swept"); return 1; }
  console.log("PASS"); return 0;
}
