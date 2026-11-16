// Bounded release of one persistent-profile browser.
//
// Stock Chrome can take many seconds, sometimes minutes, to exit after a graceful close once it has
// been running for about half a minute, and Playwright waits for a persistent context's process
// without a limit. Releasing a workspace must not depend on that: ask for a graceful close, wait a
// fixed time, then end the browser that owns this profile. Workspace state lives in the journal,
// which is already safe against process death; a forced exit can lose the profile's last
// unflushed site data (for example a cookie set moments before closing).
import { execFileSync } from 'node:child_process';

/** Main browser processes (not renderers or helpers) launched with exactly this profile directory. */
export function profileBrowserPids(profileDir, { list = () => execFileSync('ps', ['-axo', 'pid=,command='], { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }) } = {}) {
  if (process.platform === 'win32') return [];
  const flag = `--user-data-dir=${profileDir}`;
  return list().split('\n').flatMap((line) => {
    const match = /^\s*(\d+)\s+(.*)$/.exec(line);
    if (!match || match[2].includes(' --type=')) return [];
    const at = match[2].indexOf(flag), next = match[2][at + flag.length];
    return at >= 0 && (next === undefined || next === ' ') ? [Number(match[1])] : [];
  });
}

/** Resolves to 'graceful', or 'forced' after the deadline when the owning browser had to be ended. */
export async function closeWithin(context, { profileDir, timeoutMs, settleMs = 1000, findPids = profileBrowserPids, kill = process.kill.bind(process) }) {
  let timer;
  const closing = context.close().then(() => 'graceful');
  // A rejected close after a forced exit is expected; never leave it unhandled.
  closing.catch(() => {});
  const first = await Promise.race([closing.catch(() => 'graceful'), new Promise((resolve) => { timer = setTimeout(() => resolve('deadline'), timeoutMs); })]);
  clearTimeout(timer);
  if (first !== 'deadline') return 'graceful';
  for (const pid of findPids(profileDir)) {
    try { kill(pid, 'SIGKILL'); } catch { /* It exited between the listing and the signal. */ }
  }
  // Playwright's close does not always settle after its browser is ended, so it is given a short
  // grace period and then abandoned; the process it was waiting for no longer exists.
  await Promise.race([closing.catch(() => {}), new Promise((resolve) => { timer = setTimeout(resolve, settleMs); })]);
  clearTimeout(timer);
  return 'forced';
}
