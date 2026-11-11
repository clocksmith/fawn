// Unit test for C-stage platform freshness checks in platform-staging.js.

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';

import {
  cLibraryName,
  cRuntimeDirectory,
} from '../../scripts/c-native-stage.js';
import {
  stagedPlatformProblems,
  writeAddonSourceStamp,
} from '../../scripts/platform-staging.js';

function sha256Text(text) {
  return createHash('sha256').update(text).digest('hex');
}

function compilerNameFor(libraryName) {
  const ext = libraryName.slice(libraryName.lastIndexOf('.'));
  return ext === '.dll' ? `doe_compiler${ext}` : `libdoe_compiler${ext}`;
}

{
  const workspace = mkdtempSync(path.join(tmpdir(), 'doe-gpu-c-staging-'));
  try {
    execFileSync('git', ['init', '-q'], { cwd: workspace });
    execFileSync('git', ['config', 'user.email', 'test@example.com'], { cwd: workspace });
    execFileSync('git', ['config', 'user.name', 'Test'], { cwd: workspace });

    const cSourceRel = 'runtime/c/native/src/unit.c';
    const cSourceContent = 'int doe_unit(void) { return 1; }\n';
    mkdirSync(path.join(workspace, 'runtime', 'c', 'native', 'src'), { recursive: true });
    writeFileSync(path.join(workspace, cSourceRel), cSourceContent);
    execFileSync('git', ['add', cSourceRel], { cwd: workspace });
    execFileSync('git', ['commit', '-q', '-m', 'init'], { cwd: workspace });
    const head = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: workspace, encoding: 'utf8' }).trim();

    const packageRoot = path.join(workspace, 'packages', 'doe-gpu');
    const platformRoot = path.join(workspace, 'packages', 'doe-gpu-host');
    const bridge = path.join(workspace, 'runtime', 'bridge', 'webgpu-addon');
    const runtimeDirName = cRuntimeDirectory();
    const libName = cLibraryName();
    const compName = compilerNameFor(libName);
    const cStage = path.join(workspace, '.cache', 'doe-c-packages', head);

    for (const dir of [
      path.join(packageRoot, 'build', 'Release'),
      path.join(platformRoot, 'bin', 'licenses'),
      bridge,
      path.join(cStage, runtimeDirName),
      path.join(cStage, 'licenses'),
    ]) {
      mkdirSync(dir, { recursive: true });
    }

    writeFileSync(path.join(packageRoot, 'binding.gyp'), JSON.stringify({
      targets: [{ target_name: 'doe_napi', sources: ['../../runtime/bridge/webgpu-addon/doe_napi_init.c'] }],
    }));
    writeFileSync(path.join(bridge, 'doe_napi_init.c'), 'int init;\n');
    writeFileSync(path.join(bridge, 'doe_napi_internal.h'), 'int header;\n');

    const libBytes = 'c-runtime-v1';
    const compBytes = 'c-compiler-v1';
    const licenseBytes = 'MIT\n';
    const artifacts = {
      [`${runtimeDirName}/${libName}`]: sha256Text(libBytes),
      [`${runtimeDirName}/${compName}`]: sha256Text(compBytes),
      'licenses/Doe.txt': sha256Text(licenseBytes),
    };
    if (runtimeDirName === 'bin') {
      for (const dll of ['dxcompiler.dll', 'dxil.dll', 'd3dcompiler_47.dll']) {
        writeFileSync(path.join(cStage, 'bin', dll), dll);
        writeFileSync(path.join(platformRoot, 'bin', dll), dll);
        artifacts[`bin/${dll}`] = sha256Text(dll);
      }
      mkdirSync(path.join(cStage, 'licenses', 'DXC'), { recursive: true });
      mkdirSync(path.join(platformRoot, 'bin', 'licenses', 'DXC'), { recursive: true });
      writeFileSync(path.join(cStage, 'licenses', 'DXC', 'LICENSE.txt'), licenseBytes);
      writeFileSync(path.join(platformRoot, 'bin', 'licenses', 'DXC', 'LICENSE.txt'), licenseBytes);
      artifacts['licenses/DXC/LICENSE.txt'] = sha256Text(licenseBytes);
      mkdirSync(path.join(cStage, 'licenses', 'Windows-SDK'), { recursive: true });
      mkdirSync(path.join(platformRoot, 'bin', 'licenses', 'Windows-SDK'), { recursive: true });
      for (const name of ['Windows-SDK/sdk_license.rtf', 'WINDOWS-REDISTRIBUTABLES.txt']) {
        writeFileSync(path.join(cStage, 'licenses', name), licenseBytes);
        writeFileSync(path.join(platformRoot, 'bin', 'licenses', name), licenseBytes);
        artifacts[`licenses/${name}`] = sha256Text(licenseBytes);
      }
      writeFileSync(path.join(cStage, 'NATIVE_BUILD.json'), '{}');
      writeFileSync(path.join(platformRoot, 'bin', 'NATIVE_BUILD.json'), '{}');
      artifacts['NATIVE_BUILD.json'] = sha256Text('{}');
    }

    const manifestJson = JSON.stringify({
      source_commit: head,
      source_inputs: { [cSourceRel]: sha256Text(cSourceContent) },
      artifacts,
    });

    writeFileSync(path.join(packageRoot, 'build', 'Release', 'doe_napi.node'), 'addon-v1');
    writeFileSync(path.join(platformRoot, 'bin', 'doe_napi.node'), 'addon-v1');
    writeFileSync(path.join(cStage, runtimeDirName, libName), libBytes);
    writeFileSync(path.join(platformRoot, 'bin', libName), libBytes);
    writeFileSync(path.join(cStage, runtimeDirName, compName), compBytes);
    writeFileSync(path.join(platformRoot, 'bin', compName), compBytes);
    writeFileSync(path.join(cStage, 'licenses', 'Doe.txt'), licenseBytes);
    writeFileSync(path.join(platformRoot, 'bin', 'licenses', 'Doe.txt'), licenseBytes);
    writeFileSync(path.join(cStage, 'MANIFEST.json'), manifestJson);
    writeFileSync(path.join(platformRoot, 'bin', 'doe-c-manifest.json'), manifestJson);
    writeAddonSourceStamp(path.join(packageRoot, 'build', 'Release'), packageRoot, workspace);

    const options = { platformPackageRoot: platformRoot, packageRoot, workspaceRoot: workspace };
    assert.deepEqual(stagedPlatformProblems(options), [], 'fresh C staging has no problems');

    assert.throws(
      () => stagedPlatformProblems({ ...options, provider: 'zig' }),
      /Unsupported staging provider: zig/,
      'retired zig staging provider is rejected',
    );

    writeFileSync(path.join(platformRoot, 'bin', libName), 'stale-c-runtime');
    const staleLib = stagedPlatformProblems(options);
    assert(staleLib.some((problem) => /staged library differs from the workspace build/.test(problem)));
    writeFileSync(path.join(platformRoot, 'bin', libName), libBytes);

    writeFileSync(path.join(bridge, 'doe_napi_internal.h'), 'int header2;\n');
    assert(
      stagedPlatformProblems(options).some((problem) => /workspace addon predates its sources/.test(problem)),
      'an addon source change after the build is reported',
    );
  } finally {
    rmSync(workspace, { recursive: true, force: true });
  }
}

console.log('stage-platform-freshness.test: ok');
