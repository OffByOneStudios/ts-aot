/**
 * Parse Transform
 *
 * Validates and normalizes input records.
 */

/**
 * Parse and validate records
 */
export function parseTransform(records: any[]): any[] {
    const result: any[] = [];

    for (const record of records) {
        // Skip null/undefined records
        if (record == null) continue;

        // Ensure record is an object
        if (typeof record !== 'object') {
            result.push({ value: record });
            continue;
        }

        // Normalize the record
        const normalized = normalizeRecord(record);
        result.push(normalized);
    }

    return result;
}

/**
 * Normalize a single record
 */
function normalizeRecord(record: any): any {
    const result: any = {};

    for (const key of Object.keys(record)) {
        const value = record[key];

        // Normalize key (trim, lowercase)
        const normalizedKey = key.trim();

        // Normalize value
        const normalizedValue = normalizeValue(value);

        result[normalizedKey] = normalizedValue;
    }

    return result;
}

/**
 * Normalize a value
 */
function normalizeValue(value: any): any {
    // Handle null/undefined
    if (value == null) {
        return null;
    }

    // Handle strings
    if (typeof value === 'string') {
        const trimmed = value.trim();

        // Try to parse as number
        if (/^-?\d+$/.test(trimmed)) {
            return parseInt(trimmed, 10);
        }
        if (/^-?\d+\.\d+$/.test(trimmed)) {
            return parseFloat(trimmed);
        }

        // Try to parse as boolean
        if (trimmed.toLowerCase() === 'true') return true;
        if (trimmed.toLowerCase() === 'false') return false;

        // Try to parse as date
        if (/^\d{4}-\d{2}-\d{2}/.test(trimmed)) {
            return trimmed;  // Keep as string for now
        }

        return trimmed;
    }

    // Handle arrays recursively
    if (Array.isArray(value)) {
        return value.map(normalizeValue);
    }

    // Handle nested objects recursively
    if (typeof value === 'object') {
        return normalizeRecord(value);
    }

    return value;
}
