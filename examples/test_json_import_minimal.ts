import config from './config.json';
console.log("Config loaded:", typeof config);
console.log("Config is null?", config === null);
console.log("Config is undefined?", config === undefined);
if (config) {
    console.log("Config.name:", config.name);
}
