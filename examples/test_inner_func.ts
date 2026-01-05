// Simple function declaration test
function outer(): void {
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
    
    console.log("toFinite(2):", toFinite(2));
}

outer();
