// RUN: ts-aot %s -o %t.exe && %t.exe
// OUTPUT: len 2
// OUTPUT: arr0 7
// OUTPUT: tdz-throws true
// OUTPUT: done

// Module-level `let x;` initializes to undefined in BOTH its storages (local
// slot AND __modvar_ global). The PutValue TDZ check in destructuring
// assignment reads the GLOBAL — when the initializer-less-let store skipped
// the global mirror, `let x; ({x} = o)` wrongly threw "Cannot access 'x'
// before initialization" (test262 dstr family, regressed 2026-07-14).
let length;
({ length } = [7, 8]);
console.log("len " + length);

let first;
[first] = [7];
console.log("arr0 " + first);

// The check itself must NOT be weakened: destructuring assignment BEFORE the
// declaration is a genuine TDZ violation and still throws.
let threw = false;
try {
  ({ late } = { late: 1 });
} catch (e) {
  threw = e instanceof ReferenceError;
}
let late;
console.log("tdz-throws " + threw);

console.log("done");
