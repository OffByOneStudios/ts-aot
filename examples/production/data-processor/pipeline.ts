/**
 * Production Data Processor Example
 *
 * An ETL (Extract, Transform, Load) pipeline demonstrating:
 * - File I/O operations
 * - Data transformation
 * - Pipeline composition
 * - Error handling
 * - Progress reporting
 *
 * Usage:
 *   ts-aot pipeline.ts -o processor.exe
 *   ./processor.exe input.json output.json
 */

import * as fs from 'fs';
import * as path from 'path';
import { JsonReader, CsvReader } from './io/reader';
import { JsonWriter, CsvWriter } from './io/writer';
import { parseTransform } from './transforms/parse';
import { filterTransform } from './transforms/filter';
import { formatTransform } from './transforms/format';

/**
 * Pipeline configuration
 */
interface PipelineConfig {
    inputFile: string;
    outputFile: string;
    inputFormat: 'json' | 'csv';
    outputFormat: 'json' | 'csv';
    filterField?: string;
    filterValue?: string;
    verbose: boolean;
}

/**
 * Pipeline statistics
 */
interface PipelineStats {
    recordsRead: number;
    recordsWritten: number;
    recordsFiltered: number;
    startTime: number;
    endTime?: number;
}

/**
 * Logger utility
 */
const logger = {
    info(message: string, verbose: boolean = true): void {
        if (verbose) {
            const timestamp = new Date().toISOString();
            console.log(`[${timestamp}] ${message}`);
        }
    },

    error(message: string): void {
        const timestamp = new Date().toISOString();
        console.error(`[${timestamp}] ERROR: ${message}`);
    }
};

/**
 * Parse command line arguments
 */
function parseArgs(argv: string[]): PipelineConfig | null {
    const config: PipelineConfig = {
        inputFile: '',
        outputFile: '',
        inputFormat: 'json',
        outputFormat: 'json',
        verbose: false
    };

    let i = 2;  // Skip node/exe and script path

    while (i < argv.length) {
        const arg = argv[i];

        if (arg === '-v' || arg === '--verbose') {
            config.verbose = true;
        } else if (arg === '-f' || arg === '--filter') {
            if (i + 2 < argv.length) {
                config.filterField = argv[i + 1];
                config.filterValue = argv[i + 2];
                i += 2;
            }
        } else if (arg === '--csv') {
            config.inputFormat = 'csv';
            config.outputFormat = 'csv';
        } else if (arg === '--json') {
            config.inputFormat = 'json';
            config.outputFormat = 'json';
        } else if (!config.inputFile) {
            config.inputFile = arg;
        } else if (!config.outputFile) {
            config.outputFile = arg;
        }

        i++;
    }

    if (!config.inputFile || !config.outputFile) {
        return null;
    }

    return config;
}

/**
 * Show usage information
 */
function showUsage(): void {
    console.log('Data Processor Pipeline');
    console.log('');
    console.log('Usage: processor <input-file> <output-file> [options]');
    console.log('');
    console.log('Options:');
    console.log('  -v, --verbose            Show detailed progress');
    console.log('  -f, --filter FIELD VALUE Filter records by field value');
    console.log('  --csv                    Use CSV format (default: JSON)');
    console.log('  --json                   Use JSON format');
    console.log('');
    console.log('Examples:');
    console.log('  processor data.json output.json');
    console.log('  processor data.csv output.csv --csv -v');
    console.log('  processor input.json filtered.json -f status active');
}

/**
 * Run the data processing pipeline
 */
function runPipeline(config: PipelineConfig): PipelineStats {
    const stats: PipelineStats = {
        recordsRead: 0,
        recordsWritten: 0,
        recordsFiltered: 0,
        startTime: Date.now()
    };

    logger.info(`Starting pipeline`, config.verbose);
    logger.info(`Input: ${config.inputFile} (${config.inputFormat})`, config.verbose);
    logger.info(`Output: ${config.outputFile} (${config.outputFormat})`, config.verbose);

    // Step 1: Read input
    logger.info('Reading input file...', config.verbose);
    let data: any[];

    if (config.inputFormat === 'csv') {
        data = CsvReader.read(config.inputFile);
    } else {
        data = JsonReader.read(config.inputFile);
    }

    stats.recordsRead = data.length;
    logger.info(`Read ${stats.recordsRead} records`, config.verbose);

    // Step 2: Parse/validate
    logger.info('Parsing records...', config.verbose);
    data = parseTransform(data);

    // Step 3: Filter (if configured)
    if (config.filterField && config.filterValue) {
        logger.info(`Filtering by ${config.filterField} = ${config.filterValue}...`, config.verbose);
        const beforeFilter = data.length;
        data = filterTransform(data, config.filterField, config.filterValue);
        stats.recordsFiltered = beforeFilter - data.length;
        logger.info(`Filtered ${stats.recordsFiltered} records`, config.verbose);
    }

    // Step 4: Format output
    logger.info('Formatting output...', config.verbose);
    data = formatTransform(data, config.outputFormat);

    // Step 5: Write output
    logger.info('Writing output file...', config.verbose);

    if (config.outputFormat === 'csv') {
        CsvWriter.write(config.outputFile, data);
    } else {
        JsonWriter.write(config.outputFile, data);
    }

    stats.recordsWritten = data.length;
    stats.endTime = Date.now();

    return stats;
}

/**
 * Print pipeline statistics
 */
function printStats(stats: PipelineStats): void {
    const duration = (stats.endTime || Date.now()) - stats.startTime;

    console.log('');
    console.log('Pipeline Statistics:');
    console.log(`  Records read:     ${stats.recordsRead}`);
    console.log(`  Records filtered: ${stats.recordsFiltered}`);
    console.log(`  Records written:  ${stats.recordsWritten}`);
    console.log(`  Duration:         ${duration}ms`);
    console.log(`  Throughput:       ${Math.floor(stats.recordsRead / (duration / 1000))} records/sec`);
}

/**
 * Main entry point
 */
function user_main(): number {
    const config = parseArgs(process.argv);

    if (!config) {
        showUsage();
        return 1;
    }

    // Validate input file exists
    if (!fs.existsSync(config.inputFile)) {
        logger.error(`Input file not found: ${config.inputFile}`);
        return 1;
    }

    try {
        const stats = runPipeline(config);
        printStats(stats);
        console.log('');
        console.log('Pipeline completed successfully!');
        return 0;
    } catch (err) {
        const error = err as Error;
        logger.error(error.message);

        if (config.verbose) {
            console.error('');
            console.error('Stack trace:');
            console.error(error.stack || 'No stack trace available');
        }

        return 1;
    }
}
