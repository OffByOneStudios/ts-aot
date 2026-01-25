/**
 * Data Readers
 *
 * Read data from various file formats.
 */

import * as fs from 'fs';

/**
 * JSON file reader
 */
export class JsonReader {
    /**
     * Read and parse a JSON file
     */
    static read(filePath: string): any[] {
        const content = fs.readFileSync(filePath, 'utf8');
        const data = JSON.parse(content);

        // Ensure we have an array
        if (Array.isArray(data)) {
            return data;
        }

        // If it's an object with a data property, use that
        if (data && Array.isArray(data.data)) {
            return data.data;
        }

        // If it's an object with records property
        if (data && Array.isArray(data.records)) {
            return data.records;
        }

        // Wrap single object in array
        return [data];
    }
}

/**
 * CSV file reader
 */
export class CsvReader {
    /**
     * Read and parse a CSV file
     */
    static read(filePath: string): any[] {
        const content = fs.readFileSync(filePath, 'utf8');
        const lines = content.split('\n').filter(line => line.trim() !== '');

        if (lines.length === 0) {
            return [];
        }

        // Parse header
        const headers = CsvReader.parseCsvLine(lines[0]);

        // Parse data rows
        const records: any[] = [];

        for (let i = 1; i < lines.length; i++) {
            const values = CsvReader.parseCsvLine(lines[i]);
            const record: any = {};

            for (let j = 0; j < headers.length; j++) {
                record[headers[j]] = values[j] || '';
            }

            records.push(record);
        }

        return records;
    }

    /**
     * Parse a single CSV line (handles quoted values)
     */
    private static parseCsvLine(line: string): string[] {
        const values: string[] = [];
        let current = '';
        let inQuotes = false;

        for (let i = 0; i < line.length; i++) {
            const char = line[i];

            if (char === '"') {
                if (inQuotes && line[i + 1] === '"') {
                    // Escaped quote
                    current += '"';
                    i++;
                } else {
                    // Toggle quote mode
                    inQuotes = !inQuotes;
                }
            } else if (char === ',' && !inQuotes) {
                values.push(current.trim());
                current = '';
            } else {
                current += char;
            }
        }

        // Don't forget the last value
        values.push(current.trim());

        return values;
    }
}
