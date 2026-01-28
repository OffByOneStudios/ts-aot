// Library for callback_with_object.ts integration test

export interface Data {
    id: number;
    value: string;
}

export function processWithCallback(data: Data, callback: (d: Data) => void): void {
    callback(data);
}

export function transformData(data: Data, transformer: (d: Data) => string): string {
    return transformer(data);
}
