/**
 * Regular Expression Benchmark
 *
 * Tests regex compilation, matching, and replacement performance.
 * Uses ICU regular expressions under the hood.
 */

import { benchmark, BenchmarkSuite } from '../harness/benchmark';

/**
 * Generate test text with patterns
 * Uses string.repeat() to avoid deep recursion/stack issues
 */
function generateText(lines: number): string {
    // Base line with mixed content (emails, urls, words, numbers)
    const baseLine = 'hello world user@example.com https://example.com/path 12345 test benchmark admin@company.net http://test.org/page regex pattern';

    // Build text by repeating base line
    let result = baseLine;
    for (let i = 1; i < lines; i++) {
        result = result + '\n' + baseLine;
    }
    return result;
}

/**
 * Count matches of a pattern
 */
function countMatches(text: string, pattern: RegExp): number {
    const matches = text.match(pattern);
    return matches ? matches.length : 0;
}

/**
 * Find all matches with positions
 */
function findAllMatches(text: string, pattern: RegExp): Array<{ match: string; index: number }> {
    const results: Array<{ match: string; index: number }> = [];
    let match: RegExpExecArray | null;

    // Reset lastIndex for global patterns
    pattern.lastIndex = 0;

    while ((match = pattern.exec(text)) !== null) {
        results.push({ match: match[0], index: match.index });
    }

    return results;
}

/**
 * Replace all matches
 */
function replaceAll(text: string, pattern: RegExp, replacement: string): string {
    return text.replace(pattern, replacement);
}

function user_main(): number {
    console.log('Regular Expression Benchmark');
    console.log('============================');
    console.log('');

    // Generate test data
    console.log('Generating test text...');
    const smallText = generateText(100);    // ~5KB
    const mediumText = generateText(1000);  // ~50KB
    const largeText = generateText(10000);  // ~500KB

    console.log(`Small text: ${(smallText.length / 1024).toFixed(1)} KB`);
    console.log(`Medium text: ${(mediumText.length / 1024).toFixed(1)} KB`);
    console.log(`Large text: ${(largeText.length / 1024).toFixed(1)} KB`);
    console.log('');

    // Define patterns locally to defer compilation until after text generation
    // (avoids stack exhaustion from global regex init + nested arrays)
    const EMAIL_PATTERN = /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g;
    const URL_PATTERN = /https?:\/\/[^\s]+/g;

    // Verify patterns work
    const emailCount = countMatches(smallText, EMAIL_PATTERN);
    const urlCount = countMatches(smallText, URL_PATTERN);
    console.log(`Emails found in small text: ${emailCount}`);
    console.log(`URLs found in small text: ${urlCount}`);
    console.log('');

    const suite = new BenchmarkSuite('Regular Expressions');

    // Simple matching
    suite.add('test() simple (medium)', () => {
        const pattern = /hello/;
        const result = pattern.test(mediumText);
        if (result === undefined) console.log('unreachable');
    }, { iterations: 10000, warmup: 1000 });

    // Global matching - count
    suite.add('match() emails (medium)', () => {
        const count = countMatches(mediumText, /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.add('match() emails (large)', () => {
        const count = countMatches(largeText, /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 10, warmup: 2 });

    // URL matching
    suite.add('match() URLs (medium)', () => {
        const count = countMatches(mediumText, /https?:\/\/[^\s]+/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    // Number extraction
    suite.add('match() numbers (medium)', () => {
        const count = countMatches(mediumText, /\d+/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    // Word extraction
    suite.add('match() words (medium)', () => {
        const count = countMatches(mediumText, /\b\w+\b/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 50, warmup: 5 });

    // Replacement
    suite.add('replace() emails (medium)', () => {
        const result = mediumText.replace(/[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g, '[REDACTED]');
        if (!result) console.log('unreachable');
    }, { iterations: 50, warmup: 5 });

    // exec() with positions
    suite.add('exec() all matches (small)', () => {
        const matches = findAllMatches(smallText, /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g);
        if (matches.length < 0) console.log('unreachable');
    }, { iterations: 1000, warmup: 100 });

    // Split
    suite.add('split() by newline (medium)', () => {
        const lines = mediumText.split(/\n/);
        if (lines.length < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    // Complex pattern
    suite.add('complex pattern (medium)', () => {
        // Match IP-like patterns
        const count = countMatches(mediumText, /\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b/g);
        if (count < 0) console.log('unreachable');
    }, { iterations: 100, warmup: 10 });

    suite.run();
    suite.printSummary();

    return 0;
}
