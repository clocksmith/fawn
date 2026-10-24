#!/usr/bin/env node

import { existsSync, mkdirSync, readFileSync, realpathSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { checkNativeAbiLayouts } from './native-abi-layout.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(__dirname, '..');
const WORKSPACE_ROOT = resolve(PACKAGE_ROOT, '..', '..');
const BRIDGE_ROOT = resolve(WORKSPACE_ROOT, 'runtime', 'bridge', 'webgpu-addon');
const OUTPUT_DIR = resolve(PACKAGE_ROOT, 'build', 'Release');
const OUTPUT_PATH = resolve(OUTPUT_DIR, 'doe_napi.node');
const NAPI_VERSION = '8';

const addonTarget = JSON.parse(readFileSync(resolve(PACKAGE_ROOT, 'binding.gyp'), 'utf8'))
  .targets.find((target) => target.target_name === 'doe_napi');
const sourceFiles = addonTarget.sources.map((name) => resolve(PACKAGE_ROOT, name));

function resolveNodeIncludeDir() {
  const candidates = [
    process.env.NODE_INCLUDE_DIR,
    process.env.npm_config_nodedir ? resolve(process.env.npm_config_nodedir, 'include', 'node') : null,
    process.config.variables.nodedir ? resolve(process.config.variables.nodedir, 'include', 'node') : null,
    resolve(dirname(dirname(process.execPath)), 'include', 'node'),
    // Windows Node ships without headers; `node-gyp install` caches them here.
    process.platform === 'win32' && process.env.LOCALAPPDATA
      ? resolve(process.env.LOCALAPPDATA, 'node-gyp', 'Cache', process.versions.node, 'include', 'node')
      : null,
  ];
  for (const candidate of candidates) {
    if (candidate && existsSync(resolve(candidate, 'node_api.h'))) {
      return candidate;
    }
  }
  throw new Error(
    'Unable to locate Node headers. Set NODE_INCLUDE_DIR or npm_config_nodedir, or use a Node install with include/node.',
  );
}

// The addon resolves N-API symbols from node.exe through Node's import library,
// which sits beside the cached headers (<version>/<arch>/node.lib).
function resolveNodeImportLibrary(includeDir, arch, environment) {
  const library = environment.NODE_LIB || resolve(includeDir, '..', '..', arch, 'node.lib');
  if (!existsSync(library)) {
    throw new Error(`Unable to locate ${library}. Run \`npx node-gyp install\` or set NODE_LIB.`);
  }
  return library;
}

function compilerCommand(includeDir, {
  platform = process.platform,
  arch = process.arch,
  environment = process.env,
} = {}) {
  const baseArgs = [
    `-DNAPI_VERSION=${NAPI_VERSION}`,
    `-I${includeDir}`,
    '-std=c11',
  ];
  if (platform === 'win32') {
    // LLVM's clang driver consumes the actual MSVC/Windows SDK developer
    // environment. ABI probes and the addon use the same explicit MSVC target.
    const architectures = { x64: 'x86_64', arm64: 'aarch64', ia32: 'i686' };
    const targetArch = architectures[arch];
    if (!targetArch) throw new Error(`Unsupported Windows Node architecture: ${arch}`);
    // clang's GNU driver can also inject libcmt at link time despite selecting
    // the DLL CRT above. Exclude that static CRT so headers and linker agree.
    const prefixArgs = [
      `--target=${targetArch}-pc-windows-msvc`, '-fms-runtime-lib=dll',
      '-Wl,/NODEFAULTLIB:libcmt',
    ];
    return {
      compiler: environment.CC || 'clang',
      prefixArgs,
      args: [
        ...prefixArgs,
        '-shared',
        ...baseArgs,
        ...sourceFiles,
        resolveNodeImportLibrary(includeDir, arch, environment),
        '-o',
        OUTPUT_PATH,
        `-Wl,/IMPLIB:${resolve(OUTPUT_DIR, 'doe_napi.lib')}`,
      ],
    };
  }
  const compiler = environment.CC || 'cc';
  switch (platform) {
    case 'darwin':
      return {
        compiler,
        args: [
          '-bundle',
          '-undefined',
          'dynamic_lookup',
          ...baseArgs,
          ...sourceFiles,
          '-o',
          OUTPUT_PATH,
        ],
      };
    case 'linux':
      return {
        compiler,
        args: [
          '-shared',
          '-fPIC',
          ...baseArgs,
          ...sourceFiles,
          '-o',
          OUTPUT_PATH,
        ],
      };
    default:
      throw new Error(
        `build:addon is not implemented for ${platform}; use node-gyp rebuild with packages/doe-gpu/binding.gyp if available.`,
      );
  }
}

function main() {
  const includeDir = resolveNodeIncludeDir();
  const { compiler, prefixArgs, args } = compilerCommand(includeDir);
  const checkedLayouts = checkNativeAbiLayouts({
    compiler, prefixArgs, includeDir,
    bridgeHeader: resolve(BRIDGE_ROOT, 'doe_napi_internal.h'),
    upstreamHeader: resolve(WORKSPACE_ROOT, 'runtime/zig/vendor/webgpu-headers/webgpu.h'),
  });
  mkdirSync(OUTPUT_DIR, { recursive: true });
  execFileSync(compiler, args, {
    cwd: PACKAGE_ROOT,
    stdio: 'inherit',
  });
  console.log(`doe-gpu: built native addon at ${OUTPUT_PATH} (${checkedLayouts} struct layouts match webgpu.h)`);
}

export { compilerCommand };

// Compare real paths: the module URL is the resolved file, while argv[1] keeps
// any symlink (npm link, macOS /tmp) and must still run the build.
if (process.argv[1] && realpathSync(resolve(process.argv[1])) === realpathSync(fileURLToPath(import.meta.url))) main();
