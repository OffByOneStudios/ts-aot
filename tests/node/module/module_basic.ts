// Basic module tests
import * as module from 'module';

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: builtinModules is a non-empty array
    const builtins = module.builtinModules;
    if (builtins.length > 0) {
        console.log("PASS: builtinModules is non-empty array (length=" + builtins.length + ")");
        passed++;
    } else {
        console.log("FAIL: builtinModules is empty");
        failed++;
    }

    // Test 2: builtinModules contains expected modules (check by iterating)
    let hasFs = false;
    let hasPath = false;
    let hasHttp = false;
    for (let i = 0; i < builtins.length; i++) {
        if (builtins[i] === 'fs') hasFs = true;
        if (builtins[i] === 'path') hasPath = true;
        if (builtins[i] === 'http') hasHttp = true;
    }
    if (hasFs && hasPath && hasHttp) {
        console.log("PASS: builtinModules contains fs, path, http");
        passed++;
    } else {
        console.log("FAIL: builtinModules missing expected modules (fs=" + hasFs + ", path=" + hasPath + ", http=" + hasHttp + ")");
        failed++;
    }

    // Test 3: isBuiltin returns true for built-in module
    if (module.isBuiltin('fs')) {
        console.log("PASS: isBuiltin('fs') returns true");
        passed++;
    } else {
        console.log("FAIL: isBuiltin('fs') should return true");
        failed++;
    }

    // Test 4: isBuiltin returns false for non-builtin
    if (!module.isBuiltin('nonexistent-module')) {
        console.log("PASS: isBuiltin('nonexistent-module') returns false");
        passed++;
    } else {
        console.log("FAIL: isBuiltin('nonexistent-module') should return false");
        failed++;
    }

    // Test 5: isBuiltin handles node: prefix
    if (module.isBuiltin('node:fs')) {
        console.log("PASS: isBuiltin('node:fs') returns true");
        passed++;
    } else {
        console.log("FAIL: isBuiltin('node:fs') should return true");
        failed++;
    }

    // Test 6: isBuiltin handles node: prefix for path
    if (module.isBuiltin('node:path')) {
        console.log("PASS: isBuiltin('node:path') returns true");
        passed++;
    } else {
        console.log("FAIL: isBuiltin('node:path') should return true");
        failed++;
    }

    // Test 7: isBuiltin returns false for node: with non-builtin
    if (!module.isBuiltin('node:nonexistent')) {
        console.log("PASS: isBuiltin('node:nonexistent') returns false");
        passed++;
    } else {
        console.log("FAIL: isBuiltin('node:nonexistent') should return false");
        failed++;
    }

    // Test 8: builtinModules contains zlib (check by iterating)
    let hasZlib = false;
    for (let i = 0; i < builtins.length; i++) {
        if (builtins[i] === 'zlib') hasZlib = true;
    }
    if (hasZlib) {
        console.log("PASS: builtinModules contains zlib");
        passed++;
    } else {
        console.log("FAIL: builtinModules missing zlib");
        failed++;
    }

    // Test 9: syncBuiltinESMExports runs (no try/catch since not implemented)
    module.syncBuiltinESMExports();
    console.log("PASS: syncBuiltinESMExports runs without error");
    passed++;

    // Test 10: createRequire returns null (stub behavior)
    const requireFn = module.createRequire('/test/path');
    if (requireFn === null) {
        console.log("PASS: createRequire returns null (stub behavior)");
        passed++;
    } else {
        console.log("FAIL: createRequire should return null (stub)");
        failed++;
    }

    // Test 11: module.register runs without error (stub)
    module.register('test-specifier');
    console.log("PASS: module.register runs without error");
    passed++;

    // Test 12: module.register with parentURL runs without error
    module.register('test-specifier', 'file:///parent');
    console.log("PASS: module.register with parentURL runs without error");
    passed++;

    // Test 13: module.register with all args runs without error
    module.register('test-specifier', 'file:///parent', {});
    console.log("PASS: module.register with all args runs without error");
    passed++;

    // Test 14: module.registerHooks returns an object (stub)
    const hooksResult = module.registerHooks({});
    if (hooksResult !== null && hooksResult !== undefined) {
        console.log("PASS: module.registerHooks returns an object");
        passed++;
    } else {
        console.log("FAIL: module.registerHooks should return an object");
        failed++;
    }

    // Test 15: module.findPackageJSON returns undefined (stub)
    const pkgJson = module.findPackageJSON();
    if (pkgJson === undefined) {
        console.log("PASS: module.findPackageJSON returns undefined (stub)");
        passed++;
    } else {
        console.log("FAIL: module.findPackageJSON should return undefined");
        failed++;
    }

    // Test 16: module.findPackageJSON with path returns undefined (stub)
    const pkgJson2 = module.findPackageJSON('/some/path');
    if (pkgJson2 === undefined) {
        console.log("PASS: module.findPackageJSON with path returns undefined (stub)");
        passed++;
    } else {
        console.log("FAIL: module.findPackageJSON with path should return undefined");
        failed++;
    }

    // Test 17: new module.SourceMap creates an object
    const sourceMap = new module.SourceMap('test payload');
    if (sourceMap !== null && sourceMap !== undefined) {
        console.log("PASS: new module.SourceMap creates an object");
        passed++;
    } else {
        console.log("FAIL: new module.SourceMap should create an object");
        failed++;
    }

    // Note: SourceMap.findEntry() method call is not tested because
    // it would require full method dispatch on stub objects, which
    // is beyond the scope of this stub implementation.

    console.log("\n=== Results: " + passed + " passed, " + failed + " failed ===");

    return failed > 0 ? 1 : 0;
}
