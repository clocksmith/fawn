// Operating-system process and socket evidence, independent of adapter claims.
import { execFileSync, spawnSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, readlinkSync, realpathSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const COMMAND_OPTIONS = Object.freeze({ encoding: 'utf8', timeout: 15000, maxBuffer: 16 * 1024 * 1024, windowsHide: true });

function powershell(script) {
  const prelude = "$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); ";
  return execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand',
    Buffer.from(prelude + script, 'utf16le').toString('base64')], COMMAND_OPTIONS).trim();
}

function linuxProcesses() {
  return readdirSync('/proc').filter((name) => /^\d+$/.test(name)).flatMap((name) => {
    try {
      const stat = readFileSync(`/proc/${name}/stat`, 'utf8');
      return [{ pid: Number(name), parentPid: Number(stat.slice(stat.lastIndexOf(')') + 2).split(' ')[1]),
        executable: realpathSync(`/proc/${name}/exe`),
        commandLine: readFileSync(`/proc/${name}/cmdline`, 'utf8').replaceAll('\0', ' ') }];
    } catch {
      // Unrelated protected processes and processes exiting during the snapshot.
      return [];
    }
  });
}

function macProcesses() {
  const rows = execFileSync('/bin/ps', ['-ww', '-axo', 'pid=,ppid=,stat=,comm='], COMMAND_OPTIONS);
  return rows.trim().split('\n').flatMap((line) => {
    const match = /^\s*(\d+)\s+(\d+)\s+(\S+)\s+(.+)$/.exec(line);
    if (!match) throw new Error(`unrecognized process row: ${line}`);
    // A zombie has no live executable or sockets; ps renders its old comm in
    // parentheses, which must never be interpreted as a relative image path.
    if (/[ZE]/.test(match[3])) return [];
    return [{ pid: Number(match[1]), parentPid: Number(match[2]), executable: match[4] }];
  });
}

function windowsProcesses() {
  const text = powershell('$rows=@(Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine); '
    + 'ConvertTo-Json -InputObject $rows -Compress -Depth 3');
  return JSON.parse(text).map((row) => ({ pid: row.ProcessId, parentPid: row.ParentProcessId,
    executable: row.ExecutablePath, commandLine: row.CommandLine ?? '' }));
}

function processSnapshot() {
  switch (process.platform) {
    case 'linux': return linuxProcesses();
    case 'darwin': return macProcesses();
    case 'win32': return windowsProcesses();
    default: throw new Error(`process inspection is unsupported on ${process.platform}`);
  }
}

function descendantRows(rootPid, snapshot) {
  const found = [];
  const seen = new Set([rootPid]);
  const pending = [rootPid];
  while (pending.length) {
    const parentPid = pending.shift();
    for (const row of snapshot) {
      if (row.parentPid !== parentPid || seen.has(row.pid)) continue;
      seen.add(row.pid);
      found.push(row);
      pending.push(row.pid);
    }
  }
  return found;
}

function descendants(rootPid) {
  return descendantRows(rootPid, processSnapshot()).map((row) => row.pid);
}

function lsof(args) {
  const result = spawnSync('/usr/sbin/lsof', ['-nP', ...args], COMMAND_OPTIONS);
  if (result.error) throw result.error;
  // lsof returns1 if any requested PID/file selection has no match, including
  // a helper that exited during inspection. Retain partial matches; diagnostics
  // still fail closed, and live browser roots must have text mappings below.
  if (result.status !== 0 && !(result.status === 1 && !result.stderr.trim())) {
    throw new Error(`lsof ${args.join(' ')} failed: ${result.stderr.trim() || result.status}`);
  }
  return result.stdout;
}

function linuxListeners(pids) {
  const tables = ['/proc/net/tcp', '/proc/net/tcp6'].filter(existsSync);
  if (!tables.length) throw new Error('TCP listener tables are unavailable');
  const listening = new Set();
  for (const table of tables) {
    for (const line of readFileSync(table, 'utf8').trim().split('\n').slice(1)) {
      const fields = line.trim().split(/\s+/);
      if (fields[3] === '0A') listening.add(fields[9]);
    }
  }
  let count = 0;
  for (const pid of pids) {
    try {
      for (const fd of readdirSync(`/proc/${pid}/fd`)) {
        try {
          const match = /^socket:\[(\d+)\]$/.exec(readlinkSync(`/proc/${pid}/fd/${fd}`));
          if (match && listening.has(match[1])) count++;
        } catch (error) {
          if (error.code !== 'ENOENT') throw error;
        }
      }
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
    }
  }
  return count;
}

function listenerCount(pids) {
  if (!pids.length) return 0;
  switch (process.platform) {
    case 'linux': return linuxListeners(pids);
    case 'darwin': return lsof(['-a', '-p', pids.join(','), '-iTCP', '-sTCP:LISTEN', '-Fpn'])
      .split('\n').filter((line) => line.startsWith('n')).length;
    case 'win32': {
      const ids = pids.map((pid) => {
        if (!Number.isInteger(pid) || pid <= 0) throw new Error('invalid process id');
        return pid;
      });
      return Number(powershell(`$ids=@(${ids.join(',')}); `
        + '@(Get-NetTCPConnection -ErrorAction Stop | Where-Object { $_.State -eq "Listen" -and $ids -contains $_.OwningProcess }).Count'));
    }
    default: throw new Error(`socket inspection is unsupported on ${process.platform}`);
  }
}

/** Loaded text vnodes establish the actual macOS executable and framework bytes. */
function macTextImages(pids) {
  const images = new Map();
  let pid;
  for (const line of lsof(['-a', '-p', pids.join(','), '-d', 'txt', '-Fpn']).split('\n')) {
    if (line.startsWith('p')) {
      pid = Number(line.slice(1));
      images.set(pid, []);
    } else if (line.startsWith('n')) {
      // txt includes mapped system data too. Those files can be protected or
      // transient; only the browser's own image/bundle forms its code identity.
      images.get(pid).push(line.slice(1));
    }
  }
  return images;
}

function inspectProcesses(hostPid) {
  let rows = descendantRows(hostPid, processSnapshot());
  if (process.platform === 'darwin') {
    const helper = fileURLToPath(new URL('./process-paths-macos.py', import.meta.url));
    const paths = JSON.parse(execFileSync('python3', [helper, JSON.stringify(rows.map((row) => row.pid))], COMMAND_OPTIONS));
    rows = rows.filter((row) => paths[row.pid]).map((row) => ({ ...row, executable: paths[row.pid] }));
  }
  const roots = rows.filter((row) => row.parentPid === hostPid);
  if (!roots.length) throw new Error(`no live browser process belongs to host ${hostPid}`);
  const images = process.platform === 'darwin' ? macTextImages(rows.map((row) => row.pid)) : null;
  for (const row of rows) {
    if (!row.executable) throw new Error(`executable identity unavailable for browser process ${row.pid}`);
    row.executable = realpathSync(row.executable);
    if (images && !(images.get(row.pid) ?? []).includes(row.executable)) {
      // A short-lived helper may have exited during inspection. A root must
      // retain independently observed kernel text identity to qualify.
      if (roots.includes(row)) throw new Error(`browser executable not observed in kernel text mappings: ${row.executable}`);
    }
  }
  const modules = new Set();
  const mapped = new Set();
  if (images) {
    for (const root of roots) {
      const bundle = root.executable.split('.app/')[0] + '.app/';
      for (const path of images.get(root.pid) ?? []) {
        if (!path.startsWith(bundle)) continue;
        const real = realpathSync(path);
        mapped.add(real);
        const framework = /\/([^/]+)\.framework\/(?:Versions\/[^/]+\/)?([^/]+)$/.exec(real);
        // Chromium's main framework is executable code; txt also contains
        // lazily mapped resources/libraries, recorded separately below.
        if (real === root.executable || (framework && framework[1] === framework[2])) modules.add(real);
      }
    }
  } else if (process.platform === 'win32') {
    const ids = roots.map((row) => row.pid).join(',');
    const paths = JSON.parse(powershell(`$paths=@(Get-Process -Id @(${ids}) -ErrorAction Stop | `
      + 'ForEach-Object { $_.Modules | ForEach-Object { $_.FileName } }); ConvertTo-Json -InputObject $paths -Compress'));
    for (const path of paths) {
      const real = realpathSync(path);
      if (roots.some((root) => real === root.executable) || /[\\/]chrome\.dll$/i.test(real)) modules.add(real);
    }
  }
  const listeningTcpSockets = listenerCount(rows.map((row) => row.pid));
  if (!Number.isInteger(listeningTcpSockets) || listeningTcpSockets < 0) throw new Error('invalid socket inspection result');
  return { rows, roots, codeModules: [...modules], mappedCodeModules: [...mapped], listeningTcpSockets };
}

function processesUsing(path) {
  if (process.platform === 'darwin') {
    const commands = execFileSync('/bin/ps', ['-ww', '-axo', 'pid=,args='], COMMAND_OPTIONS);
    return commands.split('\n').flatMap((line) => {
      const match = /^\s*(\d+)\s+(.*)$/.exec(line);
      return match && match[2].includes(path) ? [Number(match[1])] : [];
    });
  }
  return processSnapshot().filter((row) => row.commandLine.includes(path)).map((row) => row.pid);
}

function windowsFileVersion(path) {
  return powershell(`(Get-Item -LiteralPath '${path.replaceAll("'", "''")}').VersionInfo.ProductVersion`);
}

export { descendants, inspectProcesses, processesUsing, windowsFileVersion };
