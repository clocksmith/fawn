// Fawn workspace — narrow Playwright adapter over stock Chromium
//
// Only the service constructs this adapter. Chromium is launched over Playwright's private pipe
// with a persistent per-workspace profile, so no debugging endpoint exists for other clients.

import { createHash } from 'node:crypto';
import { mkdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { chromium } from 'playwright-core';
import { buildProbe, buildSnapshot, elementArgs, queryPage, snapshotArgs } from './page-query.js';

const UNSAFE_FILENAME_CHARS = /[^A-Za-z0-9._-]/g;

class PlaywrightWorkspaceAdapter {
  constructor({ config, profileRoot, artifactRoot }) {
    this._config = config;
    this._profileRoot = profileRoot;
    this._artifactRoot = artifactRoot;
    this._workspaces = new Map();
  }

  async openWorkspace(workspaceId) {
    if (this._workspaces.has(workspaceId)) return;
    const profileDir = join(this._profileRoot, workspaceId);
    mkdirSync(profileDir, { recursive: true });
    const context = await chromium.launchPersistentContext(profileDir, {
      headless: this._config.browser.headless,
      acceptDownloads: true,
      timeout: this._config.dispatch.actionTimeoutMs,
    });
    const page = context.pages()[0] ?? await context.newPage();
    page.setDefaultTimeout(this._config.dispatch.actionTimeoutMs);
    this._workspaces.set(workspaceId, { context, page });
  }

  _page(workspaceId) {
    const entry = this._workspaces.get(workspaceId);
    if (!entry) throw new Error(`workspace ${workspaceId} has no open browser`);
    return entry.page;
  }

  async snapshot(workspaceId) {
    const raw = await this._page(workspaceId).evaluate(queryPage, snapshotArgs(this._config));
    return buildSnapshot(raw, this._workspaces.get(workspaceId).context.pages().length);
  }

  async probeUrl(workspaceId) {
    return this._page(workspaceId).url();
  }

  async probeElement(workspaceId, selector) {
    return buildProbe(await this._page(workspaceId).evaluate(queryPage, elementArgs(selector)));
  }

  /**
   * Perform one admitted mutation. Called only by the service's dispatcher.
   */
  async execute(workspaceId, request) {
    const page = this._page(workspaceId);
    const effect = EFFECTS[request.kind];
    if (!effect) throw new Error(`unsupported action kind ${request.kind}`);
    return effect.call(this, page, request, workspaceId);
  }

  async close() {
    const contexts = [...this._workspaces.values()].map((entry) => entry.context);
    this._workspaces.clear();
    await Promise.all(contexts.map((context) => context.close()));
  }
}

const EFFECTS = Object.freeze({
  async navigate(page, request) {
    const response = await page.goto(request.target.url, { waitUntil: 'load' });
    return { status: response?.status() ?? null, finalUrl: page.url() };
  },
  async click(page, request) {
    await page.locator(request.target.selector).click();
    return { selector: request.target.selector };
  },
  async submit(page, request) {
    await page.locator(request.target.selector).click();
    return { selector: request.target.selector };
  },
  async fill(page, request) {
    await page.locator(request.target.selector).fill(request.params.value);
    return { selector: request.target.selector };
  },
  async select(page, request) {
    const selected = await page.locator(request.target.selector).selectOption(request.params.value);
    return { selector: request.target.selector, selected };
  },
  async download(page, request, workspaceId) {
    const directory = join(this._artifactRoot, workspaceId);
    mkdirSync(directory, { recursive: true });
    const [download] = await Promise.all([
      page.waitForEvent('download'),
      page.locator(request.target.selector).click(),
    ]);
    const safeName = download.suggestedFilename().replace(UNSAFE_FILENAME_CHARS, '_');
    const artifactPath = join(directory, `${request.actionId.replace(UNSAFE_FILENAME_CHARS, '_')}-${safeName}`);
    await download.saveAs(artifactPath);
    const bytes = readFileSync(artifactPath);
    return {
      artifactPath,
      suggestedFilename: download.suggestedFilename(),
      byteLength: bytes.length,
      sha256: createHash('sha256').update(bytes).digest('hex'),
    };
  },
});

function createPlaywrightAdapter(options) {
  return new PlaywrightWorkspaceAdapter(options);
}

export { createPlaywrightAdapter };
