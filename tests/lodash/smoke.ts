// Sanity check: lodash bundle loads + smallest set of fns produce
// expected results. If smoke fails the rest of the suite won't pass.

function user_main(): number {
    const _ = require('./lodash.js');
    let passed = 0;
    if (_.VERSION === "4.17.21") passed++; else { console.log("FAIL: _.VERSION", _.VERSION); return 1; }
    if (_.identity(42) === 42) passed++; else { console.log("FAIL: _.identity"); return 1; }
    if (_.isArray([])) passed++; else { console.log("FAIL: _.isArray"); return 1; }
    if (!_.isArray({})) passed++; else { console.log("FAIL: _.isArray neg"); return 1; }
    if (_.sum([1, 2, 3]) === 6) passed++; else { console.log("FAIL: _.sum"); return 1; }
    console.log("OK: smoke (" + passed + " passed)");
    return 0;
}
