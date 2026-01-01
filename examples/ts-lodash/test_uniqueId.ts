// Test uniqueId utility

import { uniqueId } from './src/util/uniqueId';

console.log("=== uniqueId Test ===\n");

// Test without prefix
const id1 = uniqueId();
const id2 = uniqueId();
const id3 = uniqueId();

console.log("uniqueId() without prefix:");
console.log("  id1 =", id1);
console.log("  id2 =", id2);
console.log("  id3 =", id3);

// Test with prefix
const userId1 = uniqueId("user_");
const userId2 = uniqueId("user_");
const itemId1 = uniqueId("item_");

console.log("\nuniquerId() with prefix:");
console.log("  uniqueId('user_') =", userId1);
console.log("  uniqueId('user_') =", userId2);
console.log("  uniqueId('item_') =", itemId1);

console.log("\n=== All tests complete ===");
