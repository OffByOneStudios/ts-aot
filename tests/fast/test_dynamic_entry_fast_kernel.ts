// The flagship "hot 5%" pattern: a DYNAMIC entry file importing a fast
// kernel module. Regression: analyzer fast-gating keyed on the ENTRY
// program's directive, so this failed with "Undefined variable NativeArray"
// — fast mode was effectively single-file. The kernel must get full fast
// lowering (typed params/returns, arena frames) while this file stays
// dynamic (the `any` below is legal here, rejected in fast files).
import { fastSumSquares } from './test_cross_module_lib';

function user_main(): number {
  const r = fastSumSquares(100);
  const note: any = { kind: "dynamic entry" };
  if (r === 328350) {
    console.log("dynamic_entry PASS r=" + r + " " + note.kind);
    return 0;
  }
  console.log("dynamic_entry FAIL r=" + r);
  return 1;
}
