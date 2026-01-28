// Facade module that re-exports from implementation
// Tests that objects work across multiple module boundaries

export { Config, Settings, getConfigValue, formatConfig } from './re_export_impl';
