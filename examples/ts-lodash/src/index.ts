/**
 * ts-lodash - TypeScript implementation of lodash for ts-aot
 * 
 * A lightweight, fully-typed implementation of common lodash functions.
 */

// Array utilities
export { chunk, compact, flatten, take, takeRight, drop, dropRight, head, first, last, tail, initial } from './array/index';

// Collection utilities
export { filter, reject, map, flatMap, reduce, reduceRight, find, findLast, findIndex, findLastIndex, every, some, forEach, forEachRight } from './collection/index';

// Utility functions
export { identity, constant, noop, times, range, rangeFromTo, rangeWithStep } from './util/index';
