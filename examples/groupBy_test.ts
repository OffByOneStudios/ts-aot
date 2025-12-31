// Test groupBy function

function groupBy<T, K>(array: T[], iteratee: (value: T) => K): Map<K, T[]> {
    const result = new Map<K, T[]>();
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        const value = array[i];
        const key = iteratee(value);
        
        if (result.has(key)) {
            const existingGroup: T[] = result.get(key) as T[];
            existingGroup.push(value);
        } else {
            const newGroup: T[] = [];
            newGroup.push(value);
            result.set(key, newGroup);
        }
        
        i = i + 1;
    }
    
    return result;
}

// Test 1: Group numbers by Math.floor
console.log("Test 1: Group by Math.floor");
const nums = [6.1, 4.2, 6.3, 4.8];
const grouped = groupBy(nums, (n: number) => Math.floor(n));

// Check that key 4 has 2 elements
const group4 = grouped.get(4) as number[];
console.log("Group 4 size (expect 2):", group4.length);

// Check that key 6 has 2 elements  
const group6 = grouped.get(6) as number[];
console.log("Group 6 size (expect 2):", group6.length);

// Test 2: Group strings by length
console.log("Test 2: Group by string length");
const words = ["one", "two", "three", "four"];
const byLen = groupBy(words, (s: string) => s.length);

const len3 = byLen.get(3) as string[];
console.log("Length 3 group size (expect 2):", len3.length);

const len5 = byLen.get(5) as string[];
console.log("Length 5 group size (expect 1):", len5.length);

console.log("groupBy tests complete!");
