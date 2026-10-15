// Linux process/DRM observations; unavailable counters remain explicit.
import { readFile, readdir } from 'node:fs/promises';

export async function processSnapshot(cdp) {
  const { processInfo } = await cdp.send('SystemInfo.getProcessInfo');
  const processes = [];
  const clients = new Map();
  for (const process of processInfo) {
    const row = { ...process, rssKiB: null, drmError: null };
    try {
      const status = await readFile(`/proc/${process.id}/status`, 'utf8');
      row.rssKiB = Number(status.match(/^VmRSS:\s+(\d+)/m)?.[1] ?? NaN) || null;
      for (const fd of await readdir(`/proc/${process.id}/fdinfo`)) {
        let info;
        try { info = await readFile(`/proc/${process.id}/fdinfo/${fd}`, 'utf8'); }
        catch (error) { if (error.code === 'ENOENT') continue; throw error; }
        const client = info.match(/^drm-client-id:\s+(\d+)/m)?.[1];
        if (!client) continue;
        const device = info.match(/^drm-pdev:\s+(.*)/m)?.[1];
        const key = `${device}:${client}`;
        if (clients.has(key)) continue;
        const counters = {};
        for (const match of info.matchAll(/^(drm-(?:memory|resident|shared|total)-[\w-]+):\s+(\d+)\s+KiB/gm)) {
          counters[match[1]] = Number(match[2]);
        }
        clients.set(key, { client, device, pid: process.id, countersKiB: counters, raw: info });
      }
    } catch (error) { row.drmError = `${error.code}: ${error.message}`; }
    processes.push(row);
  }
  return { capturedAt: new Date().toISOString(), processes, drmClients: [...clients.values()],
    memoryScope: 'Process RSS may count shared pages repeatedly; DRM client allocation counters are not physical GPU peak residency' };
}

export function percentile(values, fraction) {
  if (!values.length || values.some(value => !Number.isFinite(value) || value < 0)) {
    throw new Error('Expected nonempty finite nonnegative timing samples');
  }
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.max(0, Math.ceil(sorted.length * fraction) - 1)];
}

export function summarizeFrames(frames) {
  const summary = {};
  for (const field of ['intervalMs', 'cpuWorkMs', 'completedMs']) {
    const values = frames.map(row => row[field]).filter(value => value !== null);
    summary[field] = { count: values.length, p50: percentile(values, 0.5),
      p95: percentile(values, 0.95), estimator: 'nearest-rank' };
  }
  return summary;
}
