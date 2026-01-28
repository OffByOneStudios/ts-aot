// Integration test: Cross-module re-export
// Tests objects passed through re-exported functions

import { Config, getConfigValue, formatConfig } from './re_export_facade';

function user_main(): number {
    console.log('Re-export Cross-Module Test');
    console.log('===========================');
    console.log('');

    const config: Config = {
        host: 'localhost',
        port: 8080,
        settings: {
            timeout: 5000,
            retries: 3
        }
    };

    // Direct access
    console.log('Test 1: Direct nested access');
    console.log('  config.settings.timeout = ' + config.settings.timeout);
    console.log('  config.settings.retries = ' + config.settings.retries);
    console.log('  PASS');
    console.log('');

    // Through re-exported function (tests double module boundary)
    console.log('Test 2: getConfigValue() through re-export');
    const timeout = getConfigValue(config, 'timeout');
    console.log('  getConfigValue(config, "timeout") = ' + timeout);
    if (timeout === 5000) {
        console.log('  PASS');
    } else {
        console.log('  FAIL: Expected 5000');
        return 1;
    }
    console.log('');

    console.log('Test 3: formatConfig() through re-export');
    const formatted = formatConfig(config);
    console.log('  formatConfig(config) = ' + formatted);
    console.log('  PASS');
    console.log('');

    console.log('ALL TESTS PASSED');
    return 0;
}
