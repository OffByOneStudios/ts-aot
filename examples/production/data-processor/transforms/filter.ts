/**
 * Filter Transform
 *
 * Filters records based on field values.
 */

/**
 * Filter records by field value
 */
export function filterTransform(records: any[], field: string, value: string): any[] {
    return records.filter(record => {
        const recordValue = getNestedValue(record, field);
        return matchValue(recordValue, value);
    });
}

/**
 * Get a nested value using dot notation
 * e.g., "user.name" gets record.user.name
 */
function getNestedValue(record: any, fieldPath: string): any {
    const parts = fieldPath.split('.');
    let current = record;

    for (const part of parts) {
        if (current == null) return undefined;
        current = current[part];
    }

    return current;
}

/**
 * Match a record value against a filter value
 * Supports:
 * - Exact match
 * - Wildcard (*) at start/end
 * - Numeric comparison (>, <, >=, <=)
 * - Boolean matching
 */
function matchValue(recordValue: any, filterValue: string): boolean {
    // Handle null/undefined
    if (recordValue == null) {
        return filterValue.toLowerCase() === 'null' || filterValue === '';
    }

    const strValue = String(recordValue);

    // Wildcard matching
    if (filterValue.startsWith('*') && filterValue.endsWith('*')) {
        const pattern = filterValue.slice(1, -1);
        return strValue.toLowerCase().indexOf(pattern.toLowerCase()) >= 0;
    }
    if (filterValue.startsWith('*')) {
        const pattern = filterValue.slice(1);
        return strValue.toLowerCase().endsWith(pattern.toLowerCase());
    }
    if (filterValue.endsWith('*')) {
        const pattern = filterValue.slice(0, -1);
        return strValue.toLowerCase().startsWith(pattern.toLowerCase());
    }

    // Numeric comparison
    if (typeof recordValue === 'number') {
        if (filterValue.startsWith('>=')) {
            return recordValue >= parseFloat(filterValue.slice(2));
        }
        if (filterValue.startsWith('<=')) {
            return recordValue <= parseFloat(filterValue.slice(2));
        }
        if (filterValue.startsWith('>')) {
            return recordValue > parseFloat(filterValue.slice(1));
        }
        if (filterValue.startsWith('<')) {
            return recordValue < parseFloat(filterValue.slice(1));
        }
        if (filterValue.startsWith('!=')) {
            return recordValue !== parseFloat(filterValue.slice(2));
        }
    }

    // Boolean matching
    if (typeof recordValue === 'boolean') {
        return recordValue === (filterValue.toLowerCase() === 'true');
    }

    // Exact match (case-insensitive for strings)
    return strValue.toLowerCase() === filterValue.toLowerCase();
}

/**
 * Filter records where field exists
 */
export function filterExists(records: any[], field: string): any[] {
    return records.filter(record => {
        const value = getNestedValue(record, field);
        return value != null;
    });
}

/**
 * Filter records where field does not exist or is null
 */
export function filterNotExists(records: any[], field: string): any[] {
    return records.filter(record => {
        const value = getNestedValue(record, field);
        return value == null;
    });
}

/**
 * Filter unique records by field
 */
export function filterUnique(records: any[], field: string): any[] {
    const seen = new Set<string>();
    const result: any[] = [];

    for (const record of records) {
        const value = String(getNestedValue(record, field) ?? '');
        if (!seen.has(value)) {
            seen.add(value);
            result.push(record);
        }
    }

    return result;
}
