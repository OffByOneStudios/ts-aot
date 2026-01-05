console.log("Before require");

// Test if we can access Object
console.log("typeof Object:", typeof Object);
console.log("Object:", Object);

const _ = require("lodash");
console.log("After require");
console.log("Type of _:", typeof _);

