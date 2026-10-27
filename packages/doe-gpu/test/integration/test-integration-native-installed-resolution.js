import { spawnSync } from 'node:child_process';
import { existsSync, realpathSync } from 'node:fs';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { assertStagedPlatformFresh } from '../../scripts/platform-staging.js';
import { assertInstalledNativeProvider } from '../lib/installed-native-provider.js';
import { nativeInstallPlatform } from '../lib/native-install-platform.js';
import { npmInvocation } from '../lib/npm-invocation.js';

// These tests resolve the library shipped in the installed platform package.
// A developer DOE_WEBGPU_LIB override would bypass exactly that resolution.
if (process.env.DOE_WEBGPU_LIB) {
  console.log('installed native resolution: skipped (DOE_WEBGPU_LIB overrides installed package resolution)');
  process.exit(0);
}

const here = dirname(fileURLToPath(import.meta.url));
const packageRoot = resolve(here, '../..');
const packagesRoot = dirname(packageRoot);
const platform = nativeInstallPlatform();
const platformPackageName = platform?.packageName;

if (!platformPackageName) {
  console.log(`installed native resolution: skipped ${process.platform}-${process.arch}`);
  process.exit(0);
}

const platformPackageRoot = resolve(packagesRoot, platformPackageName);
const platformLibraryName = platform.libraryName;
if (!existsSync(resolve(platformPackageRoot, 'bin', platformLibraryName))) {
  console.log(`installed native resolution: skipped unstaged ${platformPackageName}`);
  process.exit(0);
}
assertStagedPlatformFresh({ platformPackageRoot, packageRoot, workspaceRoot: dirname(packagesRoot), libraryFilename: platformLibraryName });

const scratchAlias = await mkdtemp(join(tmpdir(), 'doe-gpu-installed-resolution-'));
const scratch = realpathSync.native(scratchAlias);
const npm = npmInvocation();

function execute(command, args, cwd = scratch) {
  return spawnSync(command, args, {
    cwd,
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  });
}

function requireSuccess(result, label) {
  if (result.status !== 0) {
    throw new Error(`${label} failed:\n${result.stdout}\n${result.stderr}`);
  }
}

function pack(directory, label) {
  const result = execute(npm.executable, [
    ...npm.prefixArgs,
    'pack',
    '--ignore-scripts',
    '--pack-destination',
    scratch,
    '--json',
  ], directory);
  requireSuccess(result, `${label} pack`);
  return resolve(scratch, JSON.parse(result.stdout)[0].filename);
}

try {
  const wrapperTarball = pack(packageRoot, 'wrapper');
  const platformTarball = pack(platformPackageRoot, 'platform');
  await writeFile(resolve(scratch, 'package.json'), `${JSON.stringify({
    name: 'doe-gpu-installed-resolution-fixture',
    private: true,
    type: 'module',
  }, null, 2)}\n`);
  const installed = execute(npm.executable, [
    ...npm.prefixArgs,
    'install',
    '--ignore-scripts',
    '--omit=optional',
    '--package-lock=false',
    '--no-audit',
    '--no-fund',
    wrapperTarball,
    platformTarball,
  ]);
  requireSuccess(installed, 'native clean install');

  const installedWrapperRoot = resolve(scratch, 'node_modules/doe-gpu');
  const installedPlatformRoot = resolve(scratch, `node_modules/${platformPackageName}`);
  const installedModuleUrl = pathToFileURL(resolve(installedWrapperRoot, 'src/index.js')).href;
  const permissionProbe = execute(process.execPath, [
    '--permission',
    '--allow-addons',
    `--allow-fs-read=${installedWrapperRoot}`,
    `--allow-fs-read=${installedPlatformRoot}`,
    '--input-type=module',
    '--eval',
    `const {providerInfo}=await import(${JSON.stringify(installedModuleUrl)});process.stdout.write(JSON.stringify(providerInfo()));`,
  ]);
  requireSuccess(permissionProbe, 'installed native Node permission probe');
  const provider = JSON.parse(permissionProbe.stdout);
  assertInstalledNativeProvider(provider, installedPlatformRoot);
  console.log(`installed native resolution: ok ${process.platform}-${process.arch}`);
} finally {
  await rm(scratchAlias, { recursive: true, force: true });
}
