// Test 105.11.5.5: Function Stored in Object
// Pattern: Object method with 'this' reference

var obj = {
    value: 42,
    getValue: function() {
        return this.value;
    },
    setValue: function(v) {
        this.value = v;
    }
};

console.log("Initial value:", obj.getValue());
obj.setValue(100);
console.log("After setValue(100):", obj.getValue());
console.log("PASS: object_method");
