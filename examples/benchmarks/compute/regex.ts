/**
 * Regular Expression Benchmark
 *
 * Tests regex compilation, matching, and replacement performance.
 * Uses ICU regular expressions under the hood.
 */

import { benchmark, BenchmarkSuite } from '../harness/benchmark';

/**
 * Generate random text with patterns
 */
function generateText(lines: number): string {
    const words = ['hello', 'world', 'test', 'benchmark', 'regex', 'pattern', 'match', 'find'];
    const emails = ['user@example.com', 'test@test.org', 'admin@company.net'];
    const urls = ['https://example.com/path', 'http://test.org/page?q=1', 'https://api.server.com/v1'];
    const numbers = ['12345', '67890', '11111', '99999'];

    const result: string[] = [];

    for (let i = 0; i < lines; i++) {
        const parts: string[] = [];

        // Add some random content
        for (let j = 0; j < 10; j++) {
            const choice = Math.floor(Math.random() * 10);
            if (choice < 4) {
                parts.push(words[Math.floor(Math.random() * words.length)]);
            } else if (choice < 6) {
                parts.push(emails[Math.floor(Math.random() * emails.length)]);
            } else if (choice < 8) {
                parts.push(urls[Math.floor(Math.random() * urls.length)]);
            } else {
                parts.push(numbers[Math.floor(Math.random() * numbers.length)]);
            }
        }

        result.push(parts.join(' '));
    }

    return result.join('\n');
}

/**
 * Email pattern
 */
const EMAIL_PATTERN = /[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}/g;

/**
 * URL pattern
 */
const URL_PATTERN = /https?:\/\/[^\s]+/g;

/**
 * Number pattern
 */
const NUMBER_PATTERN = /\d+/g;

/**
 * Word boundary pattern
 */
const WORD_PATTERN = /\b\w+\b/g;

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
