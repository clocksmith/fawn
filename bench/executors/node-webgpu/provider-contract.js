// Optional pinned-provider admission for the three-arm public package baseline.
import { createHash } from 'node:crypto';
import { readFileSync, realpathSync } from 'node:fs';
import { createRequire } from 'node:module';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

export const BACKEND_TYPES = Object.freeze({ metal: 5, vulkan: 6, d3d12: 4 });

export function providerBackendContract(backend, provider) {
  if (!backend) return null;
  if (!Object.hasOwn(BACKEND_TYPES, backend)) throw new Error(`unsupported explicit backend: ${backend}`);
  if (!['doe', 'node-webgpu'].includes(provider)) throw new Error('explicit baseline backend requires Node package providers');
  return {
    requested: backend,
    mechanism: provider === 'node-webgpu' ? 'dawn-create-backend-filter' : 'native-adapter-backend-type',
    createOptions: provider === 'node-webgpu' ? [`backend=${backend}`] : [],
    adapterOptions: provider === 'doe' ? { backend } : {},
  };
}

export function observedBackendSelection(contract, info) {
  if (!contract) return null;
  const observedBackendType = Number.isInteger(info?.backendType) ? info.backendType : null;
  const expected = BACKEND_TYPES[contract.requested];
  if ((observedBackendType !== null && observedBackendType !== expected)
      || (contract.mechanism === 'native-adapter-backend-type' && observedBackendType === null)) {
    throw new Error(`actual backend mismatch: requested ${contract.requested}, observed ${observedBackendType}`);
  }
  return { requested: contract.requested, mechanism: contract.mechanism,
    createOptions: contract.createOptions, adapterOptions: contract.adapterOptions,
    observedBackendType, enforced: true };
}

export function pinnedDawnFiles(root, platform = process.platform, arch = process.arch) {
  if (!['darwin', 'linux', 'win32'].includes(platform)) throw new Error('unsupported pinned Dawn platform');
  if (!['x64', 'arm64'].includes(arch) || (platform === 'win32' && arch !== 'x64')) {
    throw new Error('unsupported pinned Dawn architecture');
  }
  const base = join(root, 'bench/vendor/node-webgpu-package');
  const version = JSON.parse(readFileSync(join(base, 'package.json'), 'utf8')).version;
  if (version !== '0.4.0') throw new Error(`pinned Dawn version mismatch: ${version}`);
  const index = realpathSync(join(base, 'index.js'));
  const addon = realpathSync(join(base, 'dist', `${platform}-${platform === 'darwin' ? 'universal' : arch}.dawn.node`));
  const paths = [index, realpathSync(join(base, 'package.json')), addon];
  if (platform === 'win32') paths.push(realpathSync(join(base, 'dist/d3dcompiler_47.dll')));
  const inputHashes = Object.fromEntries(paths.map(path =>
    [path, createHash('sha256').update(readFileSync(path)).digest('hex')]));
  return { index, addon, version, inputHashes };
}

export async function loadPinnedDawn(root) {
  const files = pinnedDawnFiles(root);
  // This route deliberately has no catch/import('webgpu') fallback.
  const module = await import(pathToFileURL(files.index).href);
  const require = createRequire(pathToFileURL(files.index));
  if (require.cache[files.addon]?.loaded !== true || typeof module.create !== 'function' || !module.globals) {
    throw new Error('exact pinned Dawn native addon was not loaded');
  }
  return { ...module, providerInfo: () => ({
    provider: 'dawn-node-webgpu', loaded: true, loadedAddon: require.cache[files.addon]?.loaded === true,
    version: files.version, modulePath: files.index, addonPath: files.addon,
    inputHashes: files.inputHashes, importFallback: false,
  }) };
}
