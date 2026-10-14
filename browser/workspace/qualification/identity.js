// Fawn workspace qualification — source, harness and browser identities.
//
// Results name the exact bytes they ran: source trees by per-file SHA-256, and browser
// executables as actually running under the service host, read from /proc rather than from
// adapter claims.

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createRequire } from 'node:module';
import { arch, platform, release } from 'node:os';
import { existsSync, readdirSync, readFileSync, readlinkSync, realpathSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { canonicalJson } from '../src/journal.js';

const REPO_ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const EXCLUDED_DIRECTORIES = Object.freeze(['node_modules']);
const LISTEN_STATE = '0A';
const TCP_TABLES = Object.freeze(['/proc/net/tcp', '/proc/net/tcp6']);
const INODE_FIELD = 9;
const STATE_FIELD = 3;
const PPID_FIELD = 1;
const VERSION_TIMEOUT_MS = 10000;
const DEVTOOLS_PORT_FILE = 'DevToolsActivePort';
const PROFILE_RELEASE_POLL_MS = 50;
const PROFILE_RELEASE_LIMIT_MS = 15000;

function sha256File(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

function listFiles(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (entry.isDirectory()) return EXCLUDED_DIRECTORIES.includes(entry.name) ? [] : listFiles(join(directory, entry.name));
    return entry.isFile() ? [join(directory, entry.name)] : [];
  });
}

/** Digest of a source tree: sorted repo-relative paths with their SHA-256. */
function treeDigest(repoPath) {
  const root = join(REPO_ROOT, repoPath);
  const files = listFiles(root).map((path) => [relative(REPO_ROOT, path), sha256File(path)])
    .sort((a, b) => (a[0] < b[0] ? -1 : 1));
  return { path: repoPath, sha256: createHash('sha256').update(canonicalJson(files)).digest('hex'), fileCount: files.length };
}

function repoFileSha256(repoPath) {
  return sha256File(join(REPO_ROOT, repoPath));
}

function gitState() {
  try {
    const commit = execFileSync('git', ['-C', REPO_ROOT, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
    const dirty = execFileSync('git', ['-C', REPO_ROOT, 'status', '--porcelain', '--', 'browser/workspace', 'config'], {
      encoding: 'utf8',
    }).trim();
    return { commit, workingTreeChanges: dirty.length > 0 ? dirty.split('\n') : [] };
  } catch (error) {
    return { commit: null, error: error.message };
  }
}

function parentOf(pid) {
  try {
    const stat = readFileSync(`/proc/${pid}/stat`, 'utf8');
    return Number(stat.slice(stat.lastIndexOf(')') + 2).split(' ')[PPID_FIELD]);
  } catch {
    return null;
  }
}

/** Every live descendant of rootPid. */
function descendants(rootPid) {
  const children = new Map();
  for (const entry of readdirSync('/proc')) {
    if (!/^\d+$/.test(entry)) continue;
    const ppid = parentOf(entry);
    if (ppid === null) continue;
    if (!children.has(ppid)) children.set(ppid, []);
    children.get(ppid).push(Number(entry));
  }
  const found = [];
  const pending = [...(children.get(rootPid) ?? [])];
  while (pending.length > 0) {
    const pid = pending.shift();
    found.push(pid);
    pending.push(...(children.get(pid) ?? []));
  }
  return found;
}

function listeningInodes() {
  const inodes = new Set();
  for (const table of TCP_TABLES) {
    if (!existsSync(table)) continue;
    for (const line of readFileSync(table, 'utf8').trim().split('\n').slice(1)) {
      const fields = line.trim().split(/\s+/);
      if (fields[STATE_FIELD] === LISTEN_STATE) inodes.add(fields[INODE_FIELD]);
    }
  }
  return inodes;
}

function socketInodes(pid) {
  try {
    return readdirSync(`/proc/${pid}/fd`).flatMap((fd) => {
      try {
        const match = /^socket:\[(\d+)\]$/.exec(readlinkSync(`/proc/${pid}/fd/${fd}`));
        return match ? [match[1]] : [];
      } catch {
        return [];
      }
    });
  } catch {
    return [];
  }
}

function countFilesNamed(directory, name) {
  if (!existsSync(directory)) return 0;
  return listFiles(directory).filter((path) => path.endsWith(`/${name}`)).length;
}

const executableCache = new Map();
function describeExecutable(path) {
  if (!executableCache.has(path)) {
    let version = null;
    try {
      version = execFileSync(path, ['--version'], { encoding: 'utf8', timeout: VERSION_TIMEOUT_MS }).trim();
    } catch (error) {
      version = `unavailable: ${error.message.split('\n')[0]}`;
    }
    executableCache.set(path, { path, sha256: sha256File(path), byteLength: statSync(path).size, version });
  }
  return executableCache.get(path);
}

/**
 * Inspect the processes a service host started: executables, TCP listeners and DevTools port files.
 */
function inspectBrowserProcesses(hostPid, profileRoot) {
  const pids = descendants(hostPid);
  const executables = new Set();
  for (const pid of pids) {
    try {
      executables.add(realpathSync(`/proc/${pid}/exe`));
    } catch {
      // The process exited between listing and inspection.
    }
  }
  const listening = listeningInodes();
  const listeningTcpSockets = pids.flatMap(socketInodes).filter((inode) => listening.has(inode)).length;
  return {
    processCount: pids.length,
    executables: [...executables].sort().map(describeExecutable),
    listeningTcpSockets,
    devToolsActivePortFiles: countFilesNamed(profileRoot, DEVTOOLS_PORT_FILE),
  };
}

function processesUsing(path) {
  return readdirSync('/proc').filter((entry) => /^\d+$/.test(entry)).filter((pid) => {
    try {
      return readFileSync(`/proc/${pid}/cmdline`, 'utf8').includes(path);
    } catch {
      return false;
    }
  });
}

/** Wait until no process still uses a state directory (a killed host's browser exits on pipe close). */
async function waitForProfileRelease(stateDir) {
  const deadline = Date.now() + PROFILE_RELEASE_LIMIT_MS;
  while (processesUsing(stateDir).length > 0) {
    if (Date.now() > deadline) throw new Error(`browser processes still use ${stateDir}`);
    await new Promise((resolve) => setTimeout(resolve, PROFILE_RELEASE_POLL_MS));
  }
}

/** Identities a qualification or comparison result is bound to. */
function resultIdentity() {
  const require = createRequire(import.meta.url);
  return {
    caseSet: { path: 'config/fawn-workspace-qualification.json', sha256: repoFileSha256('config/fawn-workspace-qualification.json') },
    serviceConfig: { path: 'config/fawn-workspace-service.json', sha256: repoFileSha256('config/fawn-workspace-service.json') },
    comparisonPolicy: {
      path: 'config/browser-product-comparison-policy.json',
      sha256: repoFileSha256('config/browser-product-comparison-policy.json'),
    },
    serviceSource: treeDigest('browser/workspace/src'),
    harness: treeDigest('browser/workspace/qualification'),
    packageLock: { path: 'browser/workspace/package-lock.json', sha256: repoFileSha256('browser/workspace/package-lock.json') },
    fixtureSite: {
      path: 'browser/workspace/qualification/fixture-site.js',
      sha256: repoFileSha256('browser/workspace/qualification/fixture-site.js'),
    },
    node: process.version,
    playwrightCore: require('playwright-core/package.json').version,
    os: { platform: platform(), release: release(), arch: arch() },
    git: gitState(),
  };
}

export {
  REPO_ROOT,
  descendants,
  gitState,
  inspectBrowserProcesses,
  repoFileSha256,
  resultIdentity,
  sha256File,
  treeDigest,
  waitForProfileRelease,
};
