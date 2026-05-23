function user_main(): number {
    const _ = require('./lodash.js');
    console.log("_.ceil(6.004, 2):", _.ceil(6.004, 2));
    console.log("_.floor(0.046, 2):", _.floor(0.046, 2));
    console.log("_.round(4.006, 2):", _.round(4.006, 2));
    return 0;
}
