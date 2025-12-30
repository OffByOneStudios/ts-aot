// Test JSON import functionality
import config from './config.json';

// Test basic string property
console.log("Name:", config.name);

// Test number property
console.log("Version:", config.version);

// Test boolean property
console.log("Debug:", config.debug);

// Test nested object property
console.log("Max retries:", config.maxRetries);

// Test array property
console.log("First feature:", config.features[0]);
console.log("Second feature:", config.features[1]);

// Test nested object
console.log("Settings verbose:", config.settings.verbose);
console.log("Settings timeout:", config.settings.timeout);

console.log("JSON import test completed!");
