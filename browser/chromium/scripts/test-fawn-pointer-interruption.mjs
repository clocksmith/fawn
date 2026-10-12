// Input-boundary check: a window blur must release held gestures without pausing the model.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
const { chromium } = await import(process.env.FAWN_PLAYWRIGHT_MODULE || 'playwright');
const resources = new URL('../resources/', import.meta.url);
const output = path.resolve(process.env.FAWN_REPORT_DIR || 'bench/out/fawn-pointer-interruption');
await mkdir(output, { recursive: true });
const report = { passed: false, classification: 'Chrome WebGPU window-blur input event handling; no physical OS-focus or native Doe claim.', cases: [], errors: [], sources: [] };
const names = { flow: 'fawn-prismatic-fluids.html', wave: 'fawn-magnetic-fluids.html', particles: 'fawn-start.html' };
for (const file of Object.values(names)) report.sources.push({ file, sha256: createHash('sha256').update(await readFile(new URL(file, resources))).digest('hex') });
const server = http.createServer(async (req, res) => {
  const file = new URL('.' + new URL(req.url, 'http://localhost').pathname, resources);
  if (!file.href.startsWith(resources.href)) { res.writeHead(403).end(); return; }
  try { res.setHeader('Content-Type', file.pathname.endsWith('.js') ? 'text/javascript' : 'text/html'); res.end(await readFile(file)); }
  catch { res.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const browser = await chromium.launch({ channel: 'chrome', headless: true, args: JSON.parse(process.env.FAWN_CHROME_ARGS || '["--enable-unsafe-webgpu","--use-angle=vulkan","--enable-features=Vulkan","--disable-vulkan-surface"]') });
report.browser = browser.version(); report.headless = true; report.origin = process.env.FAWN_TEST_ORIGIN || 'local';
const origin = process.env.FAWN_TEST_ORIGIN || `http://127.0.0.1:${server.address().port}`;
try {
  for (const [kind, gesture] of [['flow', 'pointer'], ['wave', 'pointer'], ['particles', 'pointer'], ['particles', 'keyboard']]) {
    const page = await browser.newPage({ viewport: { width: 900, height: 900 } });
    await page.addInitScript(() => {
      globalThis.interruptionProbe = { errors: [] };
      const request = GPUAdapter.prototype.requestDevice;
      GPUAdapter.prototype.requestDevice = async function(...args) {
        interruptionProbe.adapter = { vendor: this.info.vendor, architecture: this.info.architecture, isFallbackAdapter: this.info.isFallbackAdapter };
        const device = await request.apply(this, args);
        device.addEventListener('uncapturederror', event => interruptionProbe.errors.push(event.error.message));
        return device;
      };
      const write = GPUQueue.prototype.writeBuffer;
      GPUQueue.prototype.writeBuffer = function(buffer, offset, data, ...args) {
        if (buffer.usage & GPUBufferUsage.UNIFORM) interruptionProbe.uniforms = Array.from(data);
        return write.call(this, buffer, offset, data, ...args);
      };
    });
    page.on('pageerror', e => report.errors.push(e.message));
    await page.goto(origin + '/' + names[kind]);
    await page.waitForFunction(() => !document.querySelector('#pause').disabled);
    if (gesture === 'pointer') { await page.mouse.move(450, 450); await page.mouse.down(); }
    else { await page.locator('#view').focus(); await page.keyboard.down('Space'); }
    const strengthIndex = kind === 'particles' ? 8 : 11;
    await page.waitForFunction(i => interruptionProbe.uniforms[i] === 0, strengthIndex, { timeout: 30000 });
    assert.equal(await page.evaluate(i => interruptionProbe.uniforms[i], strengthIndex), 0);
    await page.evaluate(() => dispatchEvent(new Event('blur')));
    await page.waitForTimeout(1600);
    const uniforms = await page.evaluate(() => interruptionProbe.uniforms);
    const row = { kind, gesture, guidanceAfterBlur: uniforms[strengthIndex], paused: await page.textContent('#pause') === 'Resume', adapter: await page.evaluate(() => interruptionProbe.adapter) };
    report.cases.push(row);
    assert.ok(row.guidanceAfterBlur > 0, JSON.stringify(row));
    assert.equal(row.paused, false);
    assert.equal(await page.locator('#failure').isVisible(), false);
    assert.deepEqual(await page.evaluate(() => interruptionProbe.errors), []);
    if (gesture === 'pointer') await page.mouse.up(); else await page.keyboard.up('Space');
    row.passed = true; await page.close();
  }
  assert.deepEqual(report.errors, []); report.passed = true;
} finally {
  await writeFile(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  await browser.close(); await new Promise(resolve => server.close(resolve));
}
console.log(JSON.stringify(report.cases));
