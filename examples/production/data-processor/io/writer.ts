/**
 * Data Writers
 *
 * Write data to various file formats.
 */

import * as fs from 'fs';
import * as path from 'path';

/**
 * JSON file writer
 */
export class JsonWriter {
    /**
     * Write data to a JSON file
     */
    static write(filePath: string, data: any[], pretty: boolean = true): void {
        // Ensure output directory exists
        const dir = path.dirname(filePath);
        if (dir && !fs.existsSync(dir)) {
            fs.mkdirSync(dir, { recursive: true });
        }

        const content = pretty
            ? JSON.stringify(data, null, 2)
            : JSON.stringify(data);

        fs.writeFileSync(filePath, content, 'utf8');
    }
}

/**
 * CSV file writer
 */
export class CsvWriter {
    /**
     * Write data to a CSV file
     */
    static write(filePath: string, data: any[]): void {
        if (data.length === 0) {
            fs.writeFileSync(filePath, '', 'utf8');
            return;
        }

        // Ensure output directory exists
        const dir = path.dirname(filePath);
        if (dir && !fs.existsSync(dir)) {
            fs.mkdirSync(dir, { recursive: true });
        }

        // Get all unique headers from all records
        const headers = CsvWriter.collectHeaders(data);

        // Build CSV content
        const lines: string[] = [];

        // Header row
        lines.push(headers.map(h => CsvWriter.escapeValue(h)).join(','));

        // Data rows
        for (const record of data) {
            const values = headers.map(h => CsvWriter.escapeValue(String(record[h] ?? '')));
            lines.push(values.join(','));
        }

        fs.writeFileSync(filePath, lines.join('\n'), 'utf8');
    }

    /**
     * Collect all unique headers from records
     */
    private static collectHeaders(data: any[]): string[] {
        const headerSet = new Set<string>();

        for (const record of data) {
            if (record && typeof record === 'object') {
                Object.keys(record).forEach(key => headerSet.add(key));
            }
        }

        return Array.from(headerSet);
    }

    /**
     * Escape a value for CSV (quote if needed)
     */
    private static escapeValue(value: string): string {
        // Check if quoting is needed
        if (value.indexOf(',') >= 0 || value.indexOf('"') >= 0 || value.indexOf('\n') >= 0) {
            // Escape quotes by doubling them
            const escaped = value.replace(/"/g, '""');
            return '"' + escaped + '"';
        }

        return value;
    }
}
