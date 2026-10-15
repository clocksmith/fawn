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

const MESSAGE_TERMINATOR = '\0';
const PIPE_STDIO = Object.freeze(['ignore', 'ignore', 'ignore', 'pipe', 'pipe']);
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
    this._buffer = '';
    this._closed = new Promise((resolve) => child.once('exit', resolve));
    child.stdio[EVENT_FD].on('data', (chunk) => this._receive(chunk));
    this._closed.then(() => {
      for (const waiter of this._pending.values()) waiter.reject(new Error('Chromium exited'));
      this._pending.clear();
    });
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

  send(method, params = {}, sessionId = undefined) {
    const id = this._nextId++;
    const message = { id, method, params, ...(sessionId ? { sessionId } : {}) };
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this._pending.delete(id);
        reject(new Error(`CDP ${method} timed out`));
      }, this._timeoutMs);
      this._pending.set(id, {
        resolve: (value) => { clearTimeout(timer); resolve(value); },
        reject: (error) => { clearTimeout(timer); reject(error); },
      });
      this._child.stdio[COMMAND_FD].write(`${JSON.stringify(message)}${MESSAGE_TERMINATOR}`);
    });
  }

  /** Resolve with the first event matching method and predicate. */
  waitForEvent(method, predicate = () => true) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this._listeners.delete(listener);
        reject(new Error(`CDP event ${method} not received`));
      }, this._timeoutMs);
      const listener = (message) => {
        if (message.method !== method || !predicate(message.params, message.sessionId)) return;
        clearTimeout(timer);
        this._listeners.delete(listener);
        resolve(message.params);
      };
      this._listeners.add(listener);
    });
  }

  async close() {
    try {
      await this.send('Browser.close');
    } catch {
      this._child.kill('SIGKILL');
    }
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
    const downloadDir = join(this._artifactRoot, workspaceId, 'incoming');
    mkdirSync(profileDir, { recursive: true });
    mkdirSync(downloadDir, { recursive: true });
    const headless = this._config.browser.headless ? ['--headless'] : [];
    const child = spawn(chromiumExecutable(this._config.browser.channel), [...LAUNCH_ARGS, ...headless, `--user-data-dir=${profileDir}`,
      'about:blank'], { stdio: PIPE_STDIO });
    const cdp = new CdpConnection(child, this._config.dispatch.actionTimeoutMs);
    const { targetInfos } = await cdp.send('Target.getTargets');
    const pageTarget = targetInfos.find((target) => target.type === 'page')
      ?? await cdp.send('Target.createTarget', { url: 'about:blank' });
    const { sessionId } = await cdp.send('Target.attachToTarget', { targetId: pageTarget.targetId, flatten: true });
    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Browser.setDownloadBehavior', { behavior: 'allowAndName', downloadPath: downloadDir, eventsEnabled: true });
    this._workspaces.set(workspaceId, { cdp, sessionId, downloadDir });
  }

  _entry(workspaceId) {
    const entry = this._workspaces.get(workspaceId);
    if (!entry) throw new Error(`workspace ${workspaceId} has no open browser`);
    return entry;
  }

  async _evaluate(workspaceId, expression) {
    const { cdp, sessionId } = this._entry(workspaceId);
    const response = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId);
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
  async _point(workspaceId, selector) {
    const point = await this._evaluate(workspaceId, `(() => {
      const nodes = document.querySelectorAll(${JSON.stringify(selector)});
      if (nodes.length !== 1) return { count: nodes.length };
      nodes[0].scrollIntoView({ block: 'center', inline: 'center' });
      const box = nodes[0].getBoundingClientRect();
      return { count: 1, x: box.left + box.width / 2, y: box.top + box.height / 2 };
    })()`);
    if (point.count !== 1) throw new Error(`selector ${selector} matched ${point.count} elements`);
    return point;
  }

  async _click(workspaceId, selector) {
    const { cdp, sessionId } = this._entry(workspaceId);
    const { x, y } = await this._point(workspaceId, selector);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y }, sessionId);
    for (const type of ['mousePressed', 'mouseReleased']) {
      await cdp.send('Input.dispatchMouseEvent', { type, x, y, button: LEFT_BUTTON, clickCount: SINGLE_CLICK }, sessionId);
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
    const loaded = cdp.waitForEvent('Page.loadEventFired', (_, eventSession) => eventSession === sessionId);
    const navigation = await cdp.send('Page.navigate', { url: request.target.url }, sessionId);
    if (navigation.errorText) throw new Error(`navigation failed: ${navigation.errorText}`);
    await loaded;
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
    const begun = cdp.waitForEvent('Browser.downloadWillBegin');
    await this._click(workspaceId, request.target.selector);
    const { guid, suggestedFilename } = await begun;
    const progress = await cdp.waitForEvent('Browser.downloadProgress',
      (params) => params.guid === guid && params.state !== 'inProgress');
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
