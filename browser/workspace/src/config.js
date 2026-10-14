// Fawn workspace — service configuration
//
// Loads config/fawn-workspace-service.json; every tunable the service uses comes from it.

import { readFileSync } from 'node:fs';
import { REJECTION_CODES, workspaceError } from './errors.js';

const DEFAULT_CONFIG_URL = new URL('../../../config/fawn-workspace-service.json', import.meta.url);
const SUPPORTED_SCHEMA_VERSION = 1;
const SUPPORTED_PROTOCOL_VERSION = 1;

const POSITIVE_INTEGER_FIELDS = Object.freeze([
  'credentials.secretBytes',
  'observation.maxAgeMs',
  'observation.maxTextChars',
  'observation.maxElements',
  'dispatch.actionTimeoutMs',
  'dispatch.completionTimeoutMs',
  'dispatch.completionPollMs',
]);

const NON_EMPTY_LIST_FIELDS = Object.freeze([
  'actionKinds',
  'conditionKinds',
  'reconciliationMethods',
]);

function readPath(object, dottedPath) {
  return dottedPath.split('.').reduce((value, key) => (value == null ? undefined : value[key]), object);
}

function invalid(path, expected, received) {
  return workspaceError(REJECTION_CODES.INVALID_CONFIG, `${path}: expected ${expected}`, {
    path,
    expected,
    received,
  });
}

/**
 * Validate a parsed service config; the JSON schema in config/ is the canonical contract.
 * @param {object} config
 */
function validateServiceConfig(config) {
  if (config?.schemaVersion !== SUPPORTED_SCHEMA_VERSION) {
    throw invalid('schemaVersion', SUPPORTED_SCHEMA_VERSION, config?.schemaVersion);
  }
  if (config.protocolVersion !== SUPPORTED_PROTOCOL_VERSION) {
    throw invalid('protocolVersion', SUPPORTED_PROTOCOL_VERSION, config.protocolVersion);
  }
  for (const field of POSITIVE_INTEGER_FIELDS) {
    const value = readPath(config, field);
    if (!Number.isInteger(value) || value < 1) throw invalid(field, 'positive integer', value);
  }
  for (const field of NON_EMPTY_LIST_FIELDS) {
    const value = config[field];
    if (!Array.isArray(value) || value.length === 0) throw invalid(field, 'non-empty array', value);
  }
  for (const field of ['journal.fileName', 'journal.lockFileName']) {
    const value = readPath(config, field);
    if (typeof value !== 'string' || !/^[a-z0-9.-]+$/.test(value)) {
      throw invalid(field, 'plain file name', value);
    }
  }
  if (config.browser?.transport !== 'pipe') throw invalid('browser.transport', 'pipe', config.browser?.transport);
  if (config.browser?.persistentProfiles !== true) {
    throw invalid('browser.persistentProfiles', true, config.browser?.persistentProfiles);
  }
  return Object.freeze(config);
}

function loadServiceConfig(configUrl = DEFAULT_CONFIG_URL) {
  return validateServiceConfig(JSON.parse(readFileSync(configUrl, 'utf8')));
}

export { loadServiceConfig, validateServiceConfig };
