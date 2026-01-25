/**
 * Format Transform
 *
 * Formats records for output.
 */

/**
 * Format records for output
 */
export function formatTransform(records: any[], outputFormat: 'json' | 'csv'): any[] {
    if (outputFormat === 'csv') {
        // Flatten nested objects for CSV output
        return records.map(flattenRecord);
    }

    // For JSON, return as-is
    return records;
}

/**
 * Flatten a nested record for CSV output
 * { user: { name: "John" } } -> { "user.name": "John" }
 */
function flattenRecord(record: any, prefix: string = ''): any {
    const result: any = {};

    for (const key of Object.keys(record)) {
        const value = record[key];
        const fullKey = prefix ? `${prefix}.${key}` : key;

        if (value != null && typeof value === 'object' && !Array.isArray(value)) {
            // Recursively flatten nested objects
            const nested = flattenRecord(value, fullKey);
            Object.assign(result, nested);
        } else if (Array.isArray(value)) {
            // Convert arrays to JSON string
            result[fullKey] = JSON.stringify(value);
        } else {
            result[fullKey] = value;
        }
    }

    return result;
}

/**
 * Select specific fields from records
 */
export function selectFields(records: any[], fields: string[]): any[] {
    return records.map(record => {
        const result: any = {};
        for (const field of fields) {
            if (field in record) {
                result[field] = record[field];
            }
        }
        return result;
    });
}

/**
 * Exclude specific fields from records
 */
export function excludeFields(records: any[], fields: string[]): any[] {
    const excludeSet = new Set(fields);
    return records.map(record => {
        const result: any = {};
        for (const key of Object.keys(record)) {
            if (!excludeSet.has(key)) {
                result[key] = record[key];
            }
        }
        return result;
    });
}

/**
 * Rename fields in records
 */
export function renameFields(records: any[], mapping: Record<string, string>): any[] {
    return records.map(record => {
        const result: any = {};
        for (const key of Object.keys(record)) {
            const newKey = mapping[key] || key;
            result[newKey] = record[key];
        }
        return result;
    });
}

/**
 * Add computed field to records
 */
export function addComputedField(
    records: any[],
    fieldName: string,
    compute: (record: any) => any
): any[] {
    return records.map(record => ({
        ...record,
        [fieldName]: compute(record)
    }));
}

/**
 * Sort records by field
 */
export function sortRecords(records: any[], field: string, ascending: boolean = true): any[] {
    return records.slice().sort((a, b) => {
        const valueA = a[field];
        const valueB = b[field];

        // Handle null/undefined
        if (valueA == null && valueB == null) return 0;
        if (valueA == null) return ascending ? 1 : -1;
        if (valueB == null) return ascending ? -1 : 1;

        // Compare
        let result: number;
        if (typeof valueA === 'number' && typeof valueB === 'number') {
            result = valueA - valueB;
        } else {
            result = String(valueA).localeCompare(String(valueB));
        }

        return ascending ? result : -result;
    });
}
