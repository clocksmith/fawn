// Fawn workspace — narrow raw-CDP adapter over stock Chromium
//
// Speaks the Chrome DevTools Protocol directly over --remote-debugging-pipe (fds 3 and 4), so,
// as with the Playwright adapter, no debugging endpoint exists for other clients. Only the
// service constructs it. Playwright's registry is used solely to locate the installed binary.

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync, renameSync } from 'node:fs';
import { join } from 'node:path';
import playwrightRegistry from 'playwright-core/lib/server/registry/index';
import { buildProbe, buildSnapshot, elementArgs, queryPage, snapshotArgs } from './page-query.js';
import { prepareFawnLaunch } from '../browser-launch.js';

const MESSAGE_TERMINATOR = '\0';
const PIPE_STDIO = Object.freeze(['ignore', 'ignore', 'pipe', 'pipe', 'pipe']);
const STDERR_TAIL_CHARS = 2000;
const COMMAND_FD = 3;
const EVENT_FD = 4;
const LEFT_BUTTON = 'left';
const SINGLE_CLICK = 1;
const UNSAFE_FILENAME_CHARS = /[^A-Za-z0-9._-]/g;
const LAUNCH_ARGS = Object.freeze([
  '--remote-debugging-pipe',
  '--no-first-run',
  '--no-default-browser-check',
  '--disable-background-networking',
]);

/** The executable for a configured channel, as Playwright's registry installs or finds it. */
function chromiumExecutable(channel) {
  const path = playwrightRegistry.registry.findExecutable(channel)?.executablePath();
  if (!path) throw new Error(`no installed ${channel} executable in the Playwright registry`);
  return path;
}

class CdpConnection {
  constructor(child, timeoutMs) {
    this._child = child;
    this._timeoutMs = timeoutMs;
    this._nextId = 1;
    this._pending = new Map();
    this._listeners = new Set();
    this._eventWaiters = new Set();
    this._buffer = '';
    this._stderr = '';
    this._failure = null;
    // A browser that cannot start (no display, missing libraries) exits at once; that must reject
    // the launch with its reason, never surface as an unhandled pipe error.
    this._closed = new Promise((resolve) => {
      child.once('exit', (code, signal) => resolve({ code, signal }));
      child.once('error', (error) => resolve({ code: null, signal: null, error }));
    });
    child.stdio[2].on('data', (chunk) => {
      this._stderr = (this._stderr + chunk.toString('utf8')).slice(-STDERR_TAIL_CHARS);
    });
    for (const fd of [COMMAND_FD, EVENT_FD]) child.stdio[fd].on('error', error => this._fail(error));
    child.stdio[EVENT_FD].once('end', () => this._fail(new Error('CDP event pipe closed')));
    child.stdio[EVENT_FD].on('data', (chunk) => this._receive(chunk));
    this._closed.then(({ code, signal, error }) => {
      const reason = error?.message ?? `exit ${signal ?? code}`;
      this._fail(new Error(`Chromium ended (${reason}): ${this._stderr.trim().split('\n').slice(-3).join(' | ')}`));
    });
  }

  _fail(error) {
    this._failure ??= error;
    for (const waiter of [...this._pending.values(), ...this._eventWaiters]) waiter.reject(this._failure);
  }

  _receive(chunk) {
    this._buffer += chunk.toString('utf8');
    let end;
    while ((end = this._buffer.indexOf(MESSAGE_TERMINATOR)) !== -1) {
      const message = JSON.parse(this._buffer.slice(0, end));
      this._buffer = this._buffer.slice(end + 1);
      if (message.id !== undefined) {
        const waiter = this._pending.get(message.id);
        this._pending.delete(message.id);
        if (message.error) waiter?.reject(new Error(`${message.error.message} (${message.error.code})`));
        else waiter?.resolve(message.result);
      } else {
        for (const listener of this._listeners) listener(message);
      }
    }
  }

  send(method, params = {}, sessionId = undefined, signal = undefined) {
    if (this._failure) return Promise.reject(this._failure);
    if (signal?.aborted) return Promise.reject(signal.reason);
    const id = this._nextId++;
    const message = { id, method, params, ...(sessionId ? { sessionId } : {}) };
    return new Promise((resolve, reject) => {
      let timer;
      const cleanup = () => { clearTimeout(timer); this._pending.delete(id); signal?.removeEventListener('abort', aborted); };
      const waiter = { resolve: value => { cleanup(); resolve(value); }, reject: error => { cleanup(); reject(error); } };
      const aborted = () => waiter.reject(signal.reason);
      timer = setTimeout(() => waiter.reject(new Error(`CDP ${method} timed out`)), this._timeoutMs);
      this._pending.set(id, waiter);
      signal?.addEventListener('abort', aborted, { once: true });
      this._child.stdio[COMMAND_FD].write(`${JSON.stringify(message)}${MESSAGE_TERMINATOR}`);
    });
  }

  _waitForEvent({ method, predicate = () => true, observed = () => {} }, signal) {
    const promise = new Promise((resolve, reject) => {
      let timer;
      let done = false;
      const cleanup = () => {
        clearTimeout(timer);
        this._listeners.delete(listener);
        this._eventWaiters.delete(waiter);
        signal.removeEventListener('abort', aborted);
      };
      const finish = (error, value) => {
        if (done) return;
        done = true;
        cleanup();
        if (error) reject(error); else resolve(value);
      };
      const waiter = { reject: error => finish(error) };
      const aborted = () => finish(signal.reason);
      const listener = (message) => {
        try {
          if (message.method !== method || !predicate(message.params, message.sessionId)) return;
          observed(message.params);
          finish(null, message.params);
        } catch (error) { finish(error); }
      };
      if (this._failure || signal.aborted) { finish(this._failure ?? signal.reason); return; }
      this._listeners.add(listener);
      this._eventWaiters.add(waiter);
      signal.addEventListener('abort', aborted, { once: true });
      timer = setTimeout(() => finish(new Error(`CDP event ${method} not received`)), this._timeoutMs);
    });
    // The event can fail before its command is awaited. Retain that failure
    // for the owner, without producing a detached unhandled rejection.
    promise.catch(() => {});
    return promise;
  }

  waitForEvents(specifications) {
    const controller = new AbortController();
    const events = Promise.all(specifications.map(specification =>
      this._waitForEvent(specification, controller.signal).then(value => [specification.name, value])))
      .then(entries => Object.fromEntries(entries));
    events.catch(error => controller.abort(error));
    return {
      run: async command => {
        try {
          const [result, observed] = await Promise.all([
            Promise.resolve().then(() => command(controller.signal)), events,
          ]);
          return { result, events: observed };
        } catch (error) {
          controller.abort(error);
          throw error;
        } finally { controller.abort(new Error('CDP event group finished')); }
      },
    };
  }

  async close() {
    try {
      await this.send('Browser.close');
    } catch {
      this._child.kill('SIGKILL');
    }
    await this._closed;
  }

  async abortLaunch() {
    this._child.kill('SIGKILL');
    await this._closed;
  }
}

class CdpWorkspaceAdapter {
  constructor({ config, profileRoot, artifactRoot }) {
    this._config = config;
    this._profileRoot = profileRoot;
    this._artifactRoot = artifactRoot;
    this._workspaces = new Map();
  }

  async openWorkspace(workspaceId) {
    if (this._workspaces.has(workspaceId)) return;
    const profileDir = join(this._profileRoot, workspaceId);
    const launch = prepareFawnLaunch(this._config.browser);
    const downloadDir = join(this._artifactRoot, workspaceId, 'incoming');
    mkdirSync(profileDir, { recursive: true });
    mkdirSync(downloadDir, { recursive: true });
    const headless = this._config.browser.headless ? ['--headless'] : [];
    const child = spawn(launch?.executablePath ?? chromiumExecutable(this._config.browser.channel), [...LAUNCH_ARGS, ...(launch?.args ?? []), ...headless, `--user-data-dir=${profileDir}`,
      'about:blank'], { stdio: PIPE_STDIO });
    const cdp = new CdpConnection(child, this._config.dispatch.actionTimeoutMs);
    try {
      const { targetInfos } = await cdp.send('Target.getTargets');
      const pageTarget = targetInfos.find((target) => target.type === 'page')
        ?? await cdp.send('Target.createTarget', { url: 'about:blank' });
      const { sessionId } = await cdp.send('Target.attachToTarget', { targetId: pageTarget.targetId, flatten: true });
      await cdp.send('Page.enable', {}, sessionId);
      await cdp.send('Runtime.enable', {}, sessionId);
      await cdp.send('Browser.setDownloadBehavior', { behavior: 'allowAndName', downloadPath: downloadDir, eventsEnabled: true });
      this._workspaces.set(workspaceId, { cdp, sessionId, downloadDir });
    } catch (error) {
      await cdp.abortLaunch();
      throw error;
    }
  }

  _entry(workspaceId) {
    const entry = this._workspaces.get(workspaceId);
    if (!entry) throw new Error(`workspace ${workspaceId} has no open browser`);
    return entry;
  }

  async _evaluate(workspaceId, expression, signal = undefined) {
    const { cdp, sessionId } = this._entry(workspaceId);
    const response = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId, signal);
    if (response.exceptionDetails) {
      throw new Error(response.exceptionDetails.exception?.description ?? response.exceptionDetails.text);
    }
    return response.result.value;
  }

  _query(workspaceId, args) {
    return this._evaluate(workspaceId, `(${queryPage.toString()})(${JSON.stringify(args)})`);
  }

  async snapshot(workspaceId, read = {}) {
    const { cdp } = this._entry(workspaceId);
    const raw = await this._query(workspaceId, snapshotArgs(this._config, read));
    const { targetInfos } = await cdp.send('Target.getTargets');
    return buildSnapshot(raw, targetInfos.filter((target) => target.type === 'page').length);
  }

  async probeUrl(workspaceId) {
    return this._evaluate(workspaceId, 'location.href');
  }

  async probeElement(workspaceId, selector) {
    return buildProbe(await this._query(workspaceId, elementArgs(selector)));
  }

  /** Centre of the single element matching selector, scrolled into view; fails on 0 or >1. */
  async _point(workspaceId, selector, signal = undefined) {
    const point = await this._evaluate(workspaceId, `(() => {
      const nodes = document.querySelectorAll(${JSON.stringify(selector)});
      if (nodes.length !== 1) return { count: nodes.length };
      nodes[0].scrollIntoView({ block: 'center', inline: 'center' });
      const box = nodes[0].getBoundingClientRect();
      return { count: 1, x: box.left + box.width / 2, y: box.top + box.height / 2 };
    })()`, signal);
    if (point.count !== 1) throw new Error(`selector ${selector} matched ${point.count} elements`);
    return point;
  }

  async _click(workspaceId, selector, signal = undefined) {
    const { cdp, sessionId } = this._entry(workspaceId);
    const { x, y } = await this._point(workspaceId, selector, signal);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y }, sessionId, signal);
    for (const type of ['mousePressed', 'mouseReleased']) {
      await cdp.send('Input.dispatchMouseEvent', { type, x, y, button: LEFT_BUTTON, clickCount: SINGLE_CLICK }, sessionId, signal);
    }
  }

  /**
   * Perform one admitted mutation. Called only by the service's dispatcher.
   */
  async execute(workspaceId, request) {
    const effect = EFFECTS[request.kind];
    if (!effect) throw new Error(`unsupported action kind ${request.kind}`);
    return effect.call(this, workspaceId, request);
  }

  async close() {
    const entries = [...this._workspaces.values()];
    this._workspaces.clear();
    await Promise.all(entries.map((entry) => entry.cdp.close()));
  }
}

const EFFECTS = Object.freeze({
  async navigate(workspaceId, request) {
    const { cdp, sessionId } = this._entry(workspaceId);
    const loaded = cdp.waitForEvents([{ name: 'loaded', method: 'Page.loadEventFired',
      predicate: (_, eventSession) => eventSession === sessionId }]);
    await loaded.run(async signal => {
      const navigation = await cdp.send('Page.navigate', { url: request.target.url }, sessionId, signal);
      if (navigation.errorText) throw new Error(`navigation failed: ${navigation.errorText}`);
      return navigation;
    });
    return { status: null, finalUrl: await this.probeUrl(workspaceId) };
  },
  async click(workspaceId, request) {
    await this._click(workspaceId, request.target.selector);
    return { selector: request.target.selector };
  },
  async submit(workspaceId, request) {
    await this._click(workspaceId, request.target.selector);
    return { selector: request.target.selector };
  },
  async fill(workspaceId, request) {
    const { cdp, sessionId } = this._entry(workspaceId);
    await this._point(workspaceId, request.target.selector);
    await this._evaluate(workspaceId, `(() => {
      const node = document.querySelector(${JSON.stringify(request.target.selector)});
      node.focus();
      node.value = '';
      node.dispatchEvent(new Event('input', { bubbles: true }));
    })()`);
    await cdp.send('Input.insertText', { text: request.params.value }, sessionId);
    return { selector: request.target.selector };
  },
  async select(workspaceId, request) {
    const selected = await this._evaluate(workspaceId, `(() => {
      const node = document.querySelector(${JSON.stringify(request.target.selector)});
      node.value = ${JSON.stringify(request.params.value)};
      node.dispatchEvent(new Event('input', { bubbles: true }));
      node.dispatchEvent(new Event('change', { bubbles: true }));
      return [node.value];
    })()`);
    return { selector: request.target.selector, selected };
  },
  async download(workspaceId, request) {
    const { cdp, downloadDir } = this._entry(workspaceId);
    let guid;
    const download = cdp.waitForEvents([
      { name: 'begun', method: 'Browser.downloadWillBegin', observed: params => { guid = params.guid; } },
      { name: 'progress', method: 'Browser.downloadProgress', predicate: params => params.guid === guid && params.state !== 'inProgress' },
    ]);
    const { events: { begun: { suggestedFilename }, progress } } = await download.run(signal =>
      this._click(workspaceId, request.target.selector, signal));
    if (progress.state !== 'completed') throw new Error(`download ${guid} ${progress.state}`);
    const safeName = suggestedFilename.replace(UNSAFE_FILENAME_CHARS, '_');
    const artifactPath = join(downloadDir, '..', `${request.actionId.replace(UNSAFE_FILENAME_CHARS, '_')}-${safeName}`);
    renameSync(join(downloadDir, guid), artifactPath);
    const bytes = readFileSync(artifactPath);
    return {
      artifactPath,
      suggestedFilename,
      byteLength: bytes.length,
      sha256: createHash('sha256').update(bytes).digest('hex'),
    };
  },
});

function createCdpAdapter(options) {
  return new CdpWorkspaceAdapter(options);
}

export { createCdpAdapter };
