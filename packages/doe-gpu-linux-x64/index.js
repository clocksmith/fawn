import { existsSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const __dirname = dirname(fileURLToPath(import.meta.url));
export const addonPath = resolve(__dirname, 'bin', 'doe_napi.node');
// The Zig drop-in when staged; otherwise the C runtime (libwebgpu_doe_c.so).
const zigLibraryPath = resolve(__dirname, 'bin', 'libwebgpu_doe.so');
export const libraryPath = existsSync(zigLibraryPath) ? zigLibraryPath : resolve(__dirname, 'bin', 'libwebgpu_doe_c.so');
