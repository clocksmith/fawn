import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  buildCaptureCampaignPlan,
  resolveGraphicsLaunchArgs,
  startLocalStaticWorkloadServer,
} from '../../../../bench/external-projects/webgpu-graphics-campaigns/capture_campaign.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');

const planVk = buildCaptureCampaignPlan({ repoRoot: root, backendId: 'amd-vulkan' });
assert.equal(planVk.graphicsWorkloads.length, 4);
for (const w of planVk.graphicsWorkloads) {
  assert.equal(w.headless, false, `${w.workloadId}: must use visible window for displayed canvas`);
  assert.equal(w.sandbox, 'enabled', `${w.workloadId}: must preserve Chromium sandbox`);
  assert.ok(!w.browserArgs.includes('--no-sandbox'));
  assert.ok(!w.browserArgs.includes('--disable-vulkan-surface'));
}

const threeManifest = JSON.parse(
  readFileSync(join(root, 'bench/external-projects/webgpu-graphics-campaigns/three-fog-scattering.workload.json'), 'utf8'),
);
assert.throws(
  () => resolveGraphicsLaunchArgs(threeManifest, 'amd-vulkan', ['--no-sandbox']),
  /forbidden browser launch argument/,
);
assert.throws(
  () => resolveGraphicsLaunchArgs(threeManifest, 'amd-vulkan', ['--disable-vulkan-surface']),
  /forbidden browser launch argument/,
);

const srv = await startLocalStaticWorkloadServer(join(root, 'bench/external-projects/webgpu-graphics-campaigns'));
try {
  const res = await fetch(`${srv.origin}/README.md`);
  assert.equal(res.status, 200);
  const postRes = await fetch(`${srv.origin}/README.md`, { method: 'POST' });
  assert.equal(postRes.status, 405);
} finally {
  await srv.close();
}

console.log('ok: C2 third-party graphics capture harness plan, sandbox/surface flag guards and static server');
