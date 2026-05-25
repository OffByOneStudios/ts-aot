// ts-aot entry for running lodash's official test/test.js.
//
// Pre-sets the globals test.js probes (QUnit, _, lodashStable) so it
// skips its npm requires (qunit-extras, interopRequire of filePath),
// then drives the registered QUnit queue and prints a parseable tally:
//   LODASH-QUNIT PASS: <n>  FAIL: <n>  TOTAL: <n>
//
// Layout (created by setup.py in this directory):
//   ./qunit_shim.js   - committed minimal QUnit shim
//   ./test.js         - fetched from lodash 4.17.21 git tag (NOT committed)
//   ./lodash.js       - lodash 4.17.21 bundle (NOT committed; copied from ../lodash.js)

require('./qunit_shim.js');

var lodash = require('./lodash.js');
global._ = lodash;
global.lodashStable = lodash;

try {
  require('./test.js');
} catch (e) {
  console.log('TEST.JS TOP-LEVEL THREW: ' + (e && e.stack ? e.stack : e));
}

global.QUnit.__run();
