/**
 * Creates an array of numbers from start up to, but not including, end.
 * 
 * @example
 * range(4) // => [0, 1, 2, 3]
 * range(1, 5) // => [1, 2, 3, 4]
 * range(0, 20, 5) // => [0, 5, 10, 15]
 * range(0, -4, -1) // => [0, -1, -2, -3]
 */
export function range(end: number): number[] {
    const result: number[] = [];
    let current = 0;
    
    while (current < end) {
        result.push(current);
        current = current + 1;
    }
    
    return result;
}

/**
 * Creates an array of numbers from start up to, but not including, end.
 */
export function rangeFromTo(start: number, end: number): number[] {
    const result: number[] = [];
    let current = start;
    
    if (start <= end) {
        while (current < end) {
            result.push(current);
            current = current + 1;
        }
    } else {
        while (current > end) {
            result.push(current);
            current = current - 1;
        }
    }
    
    return result;
}

/**
 * Creates an array of numbers from start up to, but not including, end, with step.
 */
export function rangeWithStep(start: number, end: number, step: number): number[] {
    if (step === 0) {
        return [];
    }
    
    const result: number[] = [];
    let current = start;
    
    if (step > 0) {
        while (current < end) {
            result.push(current);
            current = current + step;
        }
    } else {
        while (current > end) {
            result.push(current);
            current = current + step;
        }
    }
    
    return result;
}
