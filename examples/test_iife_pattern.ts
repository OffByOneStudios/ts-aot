// Simulate lodash pattern - IIFE with internal function calls during init
(function() {
    function toNumber(value: any): any {
        if (typeof value == 'number') {
            return value;
        }
        return 0;
    }
    
    function toFinite(value: any): any {
        const result = toNumber(value);
        return result;
    }
    
    // This is called DURING the IIFE execution, not after
    const initValue = toFinite(42);
    console.log("initValue:", initValue);
})();
