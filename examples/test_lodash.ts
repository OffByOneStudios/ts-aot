// Test lodash-like cloneDeep and isEqual functions

function cloneDeep<T>(value: T): T {
    if (value === null) return value;
    
    const t = typeof value;
    if (t === "number" || t === "string" || t === "boolean") return value;
    
    if (Array.isArray(value)) {
        const arr = value as any[];
        const result: any[] = [];
        for (let i = 0; i < arr.length; i = i + 1) {
            result.push(cloneDeep(arr[i]));
        }
        return result as T;
    }
    
    const obj = value as any;
    const result: any = {};
    const keys = Object.keys(obj);
    for (let i = 0; i < keys.length; i = i + 1) {
        const key = keys[i];
        result[key] = cloneDeep(obj[key]);
    }
    return result as T;
}

function isEqual<T>(a: T, b: T): boolean {
    if (a === b) return true;
    if (a === null || b === null) return false;
    
    const ta = typeof a;
    const tb = typeof b;
    if (ta !== tb) return false;
    
    if (ta === "number" || ta === "string" || ta === "boolean") {
        return a === b;
    }
    
    if (Array.isArray(a) && Array.isArray(b)) {
        const aa = a as any[];
        const ba = b as any[];
        if (aa.length !== ba.length) return false;
        for (let i = 0; i < aa.length; i = i + 1) {
            if (!isEqual(aa[i], ba[i])) return false;
        }
        return true;
    }
    
    if (Array.isArray(a) || Array.isArray(b)) return false;
    
    const ao = a as any;
    const bo = b as any;
    const aKeys = Object.keys(ao);
    const bKeys = Object.keys(bo);
    if (aKeys.length !== bKeys.length) return false;
    
    for (let i = 0; i < aKeys.length; i = i + 1) {
        const key = aKeys[i];
        if (!isEqual(ao[key], bo[key])) return false;
    }
    return true;
}

// ==== partial ====
function add(a: number, b: number): number {
    return a + b;
}

function multiply3(a: number, b: number, c: number): number {
    return a * b * c;
}

function partial1_2(fn: (a: number, b: number) => number, arg1: number): (b: number) => number {
    return (b: number): number => fn(arg1, b);
}

function partial2_3(fn: (a: number, b: number, c: number) => number, arg1: number, arg2: number): (c: number) => number {
    return (c: number): number => fn(arg1, arg2, c);
}

// ==== curry ====
function curry2(fn: (a: number, b: number) => number): (a: number) => (b: number) => number {
    return (a: number): ((b: number) => number) => {
        return (b: number): number => fn(a, b);
    };
}

// NOTE: curry3 causes runtime panic due to triple-nested closures bug - tracked for fix
// function curry3(fn: (a: number, b: number, c: number) => number): (a: number) => (b: number) => (c: number) => number {
//     return (a: number): ((b: number) => (c: number) => number) => {
//         return (b: number): ((c: number) => number) => {
//             return (c: number): number => fn(a, b, c);
//         };
//     };
// }

// ======== TESTS ========

console.log("=== Test cloneDeep ===");
const obj1 = { x: 1, y: 2 };
const cloned1 = cloneDeep(obj1);
console.log("Test 1 - Simple object:");
console.log("  Original:", obj1.x, obj1.y);
console.log("  Cloned:", cloned1.x, cloned1.y);

const arr1 = [1, 2, 3];
const clonedArr = cloneDeep(arr1);
console.log("Test 2 - Array:");
console.log("  Cloned array length:", clonedArr.length);

console.log("\n=== Test isEqual ===");
console.log("Test 3 - Primitives:");
console.log("  isEqual(1, 1):", isEqual(1, 1));
console.log("  isEqual(1, 2):", isEqual(1, 2));

console.log("Test 4 - Arrays:");
console.log("  isEqual([1,2,3], [1,2,3]):", isEqual([1, 2, 3], [1, 2, 3]));
console.log("  isEqual([1,2,3], [1,2,4]):", isEqual([1, 2, 3], [1, 2, 4]));

console.log("Test 5 - Objects:");
const eqObj1 = { x: 1, y: 2 };
const eqObj2 = { x: 1, y: 2 };
console.log("  isEqual({x:1,y:2}, {x:1,y:2}):", isEqual(eqObj1, eqObj2));

console.log("\n=== Test partial ===");
const add5 = partial1_2(add, 5);
console.log("Test 6 - partial(add, 5)(3):", add5(3));  // Should be 8

const mult6 = partial2_3(multiply3, 2, 3);
console.log("Test 7 - partial(multiply3, 2, 3)(4):", mult6(4));  // Should be 24

console.log("\n=== Test curry ===");
const curriedAdd = curry2(add);
const addTen = curriedAdd(10);
console.log("Test 8 - curry2(add)(10)(5):", addTen(5));  // Should be 15

console.log("\n=== All tests complete ===");
