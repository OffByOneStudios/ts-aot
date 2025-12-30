// Bug: Generic callback type issue
// Predicate callbacks in generic functions get wrong parameter types

function filter<T>(array: T[], predicate: (value: T) => boolean): T[] {
    const result: T[] = [];
    let i = 0;
    const len = array.length;
    
    while (i < len) {
        if (predicate(array[i])) {  // predicate gets ptr param instead of int
            result.push(array[i]);
        }
        i = i + 1;
    }
    
    return result;
}

const nums = [1, 2, 3, 4, 5];
const evens = filter(nums, (n) => n % 2 === 0);  // n should be number, not ptr
console.log(evens.length);  // Should be 2
