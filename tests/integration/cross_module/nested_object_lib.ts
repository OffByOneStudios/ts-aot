// Library module - exports function that accesses nested property

export interface Timing {
    min: number;
    max: number;
    avg: number;
}

export interface Result {
    name: string;
    iterations: number;
    timing: Timing;
}

export function getTimingMin(result: Result): number {
    return result.timing.min;
}

export function getTimingAvg(result: Result): number {
    return result.timing.avg;
}

export function formatResult(result: Result): string {
    return 'Name: ' + result.name + ', Min: ' + result.timing.min + ', Avg: ' + result.timing.avg;
}
