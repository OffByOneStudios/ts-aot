// Simplest callback test

function myFn(): void {
    console.log("myFn executed");
}

function caller(fn: () => void): void {
    console.log("caller: about to call fn");
    fn();
    console.log("caller: called fn");
}

caller(myFn);
console.log("done");
