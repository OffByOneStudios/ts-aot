import EventEmitter from 'eventemitter3';

function user_main(): number {
    const ee: any = new EventEmitter();
    console.log("type: " + typeof ee);
    console.log("_eventsCount: " + String(ee._eventsCount));
    console.log("prefixed: " + String((EventEmitter as any).prefixed));

    const keys = Object.keys(ee);
    console.log("keys.length: " + String(keys.length));

    return 0;
}
