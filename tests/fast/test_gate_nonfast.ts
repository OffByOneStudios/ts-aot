// No "use fast" directive: the fast subset must be entirely inert here.
// 'struct' is an ordinary identifier and none of the FastCheck rules apply,
// so this ordinary (non-fast) file compiles and runs normally.
function user_main(): number {
  let struct: number = 5;      // 'struct' as a plain identifier
  const obj: any = { a: 1 };   // 'any' is fine outside a fast file
  delete obj.a;                // so is delete
  console.log("gate_nonfast struct=" + struct);
  return 0;
}
