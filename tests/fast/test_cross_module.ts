// fast->fast cross-module: an imported fast kernel must resolve to its
// module-mangled specialization (typed direct call), not a weak stub.
// Regression: this returned NaN (double arg into an unlinked variant).
"use fast";
import { fastSumSquares } from './test_cross_module_lib';

function user_main(): number {
  const r = fastSumSquares(100);
  if (r === 328350) {
    console.log("cross_module PASS r=" + r);
    return 0;
  }
  console.log("cross_module FAIL r=" + r);
  return 1;
}
