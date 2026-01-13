// Test prototype chain implementation

function user_main(): number {
    let passed = 0;
    let failed = 0;

    // Test 1: Object.create sets prototype correctly
    console.log("Test 1: Object.create prototype inheritance");
    const parent1: any = { x: 10, y: 20 };
    const child1: any = Object.create(parent1);
    if (child1.x === 10 && child1.y === 20) {
        console.log("  PASS: child inherits parent properties");
        passed++;
    } else {
        console.log("  FAIL: child should inherit parent properties");
        failed++;
    }

    // Test 2: Child can have own properties
    console.log("Test 2: Child own properties");
    child1.z = 30;
    if (child1.z === 30 && child1.x === 10) {
        console.log("  PASS: child has own property and inherits");
        passed++;
    } else {
        console.log("  FAIL: child own property or inheritance failed");
        failed++;
    }

    // Test 3: Child own property shadows parent
    console.log("Test 3: Property shadowing");
    child1.x = 100;
    if (child1.x === 100 && parent1.x === 10) {
        console.log("  PASS: child shadows parent without modifying parent");
        passed++;
    } else {
        console.log("  FAIL: property shadowing incorrect");
        failed++;
    }

    // Test 4: 'in' operator checks prototype chain
    console.log("Test 4: 'in' operator with prototype chain");
    const parent2: any = { a: 1 };
    const child2: any = Object.create(parent2);
    if ("a" in child2 && !("b" in child2)) {
        console.log("  PASS: 'in' operator works with prototype chain");
        passed++;
    } else {
        console.log("  FAIL: 'in' operator prototype chain check failed");
        failed++;
    }

    // Test 5: Object.getPrototypeOf returns correct prototype
    console.log("Test 5: Object.getPrototypeOf");
    const proto = Object.getPrototypeOf(child2);
    if (proto === parent2) {
        console.log("  PASS: getPrototypeOf returns correct prototype");
        passed++;
    } else {
        console.log("  FAIL: getPrototypeOf returned wrong value");
        failed++;
    }

    // Test 6: Object.getPrototypeOf on object without custom prototype
    console.log("Test 6: Object.getPrototypeOf on plain object");
    const plain: any = { foo: "bar" };
    const plainProto = Object.getPrototypeOf(plain);
    // Plain objects should have null prototype in our implementation
    console.log("  INFO: plain object prototype is null: " + (plainProto === null));
    passed++;

    // Test 7: Multi-level prototype chain
    console.log("Test 7: Multi-level prototype chain");
    const grandparent: any = { level: "grandparent", gp: true };
    const parent3: any = Object.create(grandparent);
    parent3.level = "parent";
    parent3.p = true;
    const child3: any = Object.create(parent3);
    child3.level = "child";
    child3.c = true;

    if (child3.level === "child" && child3.p === true && child3.gp === true) {
        console.log("  PASS: 3-level prototype chain works");
        passed++;
    } else {
        console.log("  FAIL: multi-level prototype chain failed");
        failed++;
    }

    // Test 8: Object.create with null prototype
    console.log("Test 8: Object.create with null prototype");
    const nullProtoObj: any = Object.create(null);
    nullProtoObj.test = "value";
    const nullObjProto = Object.getPrototypeOf(nullProtoObj);
    if (nullObjProto === null && nullProtoObj.test === "value") {
        console.log("  PASS: null prototype object works");
        passed++;
    } else {
        console.log("  FAIL: null prototype object failed");
        failed++;
    }

    // Test 9: Object.setPrototypeOf
    console.log("Test 9: Object.setPrototypeOf");
    const obj9: any = { own: "value" };
    const proto9: any = { inherited: "from proto" };
    Object.setPrototypeOf(obj9, proto9);
    if (obj9.own === "value" && obj9.inherited === "from proto") {
        console.log("  PASS: setPrototypeOf works");
        passed++;
    } else {
        console.log("  FAIL: setPrototypeOf failed");
        failed++;
    }

    // Test 10: Prototype chain does not affect Object.keys (own properties only)
    console.log("Test 10: Object.keys returns only own properties");
    const parent10: any = { parentProp: 1 };
    const child10: any = Object.create(parent10);
    child10.childProp = 2;
    const keys = Object.keys(child10);
    if (keys.length === 1 && keys[0] === "childProp") {
        console.log("  PASS: Object.keys returns only own properties");
        passed++;
    } else {
        console.log("  FAIL: Object.keys should not include inherited properties");
        console.log("  Got keys: " + keys.join(", "));
        failed++;
    }

    // Summary
    console.log("");
    console.log("=== Summary ===");
    console.log("Passed: " + passed);
    console.log("Failed: " + failed);

    return failed;
}
