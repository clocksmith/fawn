import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
const bin = new URL('./bin/', import.meta.url);
const { native_library: library } = JSON.parse(readFileSync(new URL('doe-c-manifest.json', bin), 'utf8'));
export const addonPath = fileURLToPath(new URL('doe_napi.node', bin));
export const libraryPath = fileURLToPath(new URL(library.filename, bin));
