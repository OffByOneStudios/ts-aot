// RUN: %ts-aot %s --dump-ir -o %t.exe && %t.exe
// CHECK: define
// OUTPUT: string
// OUTPUT: 0
// OUTPUT: string
// OUTPUT: 20

// Compound `+=` with a string on an array element / object property must
// lower identically to the explicit `el = el + rhs` form. A regression
// previously emitted a low-level StringConcat over the boxed element and
// stored the result via ts_value_make_object, so the TsString was wrapped
// as a generic object and read back as `undefined`.
function user_main() {
  var arr = [1, 0, [1, 2, 3]];
  arr[1] += '';
  console.log(typeof arr[1]);   // string
  console.log(arr[1]);          // 0

  var o = { p: 20 };
  o.p += '';
  console.log(typeof o.p);      // string
  console.log(o.p);             // 20
  return 0;
}
