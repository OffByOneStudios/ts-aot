// Implementation module for re-export test
// This module is re-exported by re_export_facade.ts

export interface Settings {
    timeout: number;
    retries: number;
}

export interface Config {
    host: string;
    port: number;
    settings: Settings;
}

export function getConfigValue(config: Config, key: string): number {
    if (key === 'timeout') {
        return config.settings.timeout;
    } else if (key === 'retries') {
        return config.settings.retries;
    } else if (key === 'port') {
        return config.port;
    }
    return -1;
}

export function formatConfig(config: Config): string {
    return config.host + ':' + config.port + ' (timeout=' + config.settings.timeout + 'ms)';
}
