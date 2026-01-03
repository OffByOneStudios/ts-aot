// Minimal test mimicking lodash tag map creation
const cloneableTags: any = {};

console.log("Creating cloneableTags...");

// Set multiple properties like lodash does
cloneableTags["[object Array]"] = true;
console.log("Set [object Array]");

cloneableTags["[object Function]"] = true;
console.log("Set [object Function]");

cloneableTags["[object Error]"] = true;
console.log("Set [object Error]");

// Try to access the object after setting properties
console.log("Accessing cloneableTags:", cloneableTags);

// See if retrieving a property triggers the issue  
const testVal = cloneableTags["[object Error]"];
console.log("Retrieved value:", testVal);

console.log("Test completed successfully!");
