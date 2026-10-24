// Mechanical rules from STYLE.md: kebab-case .js sources, no .mjs/.cjs except
// fixtures that run outside this package, no CommonJS require outside the
// native addon loader, no TODO/FIXME, and test file naming per directory.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const PACKAGE_ROOT = fileURLToPath(new URL('../..', import.meta.url));
// ESM cannot import a .node addon; this loader uses createRequire for it alone.
const REQUIRE_ALLOWED = new Set(['src/vendor/webgpu/index.js']);
const TEST_NAMES = [
  [/^test\/unit\//, /^[a-z0-9]+(-[a-z0-9]+)*\.test\.js$/],
  [/^test\/(smoke|integration)\//, /^(test-[a-z0-9]+(-[a-z0-9]+)*|run-integration|[a-z0-9]+(-[a-z0-9]+)*-helper|first-kernel-receipt-test)\.js$/],
];

const files = execFileSync('git', ['ls-files', 'src', 'test', 'scripts'], { cwd: PACKAGE_ROOT, encoding: 'utf8' })
  .split('\n').filter((path) => /\.(m|c)?js$/.test(path));
const failures = [];
for (const path of files) {
  const name = path.split('/').pop();
  // Fixtures run from scratch directories outside this package keep .mjs.
  if (!path.startsWith('test/fixtures/') && /\.(m|c)js$/.test(name)) failures.push(`${path}: use .js (ESM is declared in package.json)`);
  if (path.startsWith('src/') && !/^[a-z0-9]+(-[a-z0-9]+)*\.js$/.test(name)) failures.push(`${path}: source files are kebab-case.js`);
  for (const [directory, pattern] of TEST_NAMES) {
    if (directory.test(path) && !path.slice(path.lastIndexOf('/') + 1).match(pattern)) {
      failures.push(`${path}: test file name does not follow STYLE.md#file-naming`);
    }
  }
  const source = readFileSync(join(PACKAGE_ROOT, path), 'utf8');
  if (/\bTODO\b|\bFIXME\b/.test(source) && path !== 'test/unit/js-style.test.js') {
    failures.push(`${path}: no TODO/FIXME; record follow-ups in the status log`);
  }
  if (path.startsWith('src/') && /\brequire\(/.test(source) && !REQUIRE_ALLOWED.has(path)) {
    failures.push(`${path}: no CommonJS require() in package sources`);
  }
}
assert.deepEqual(failures, [], `JavaScript style violations:\n${failures.join('\n')}`);
console.log(`ok: ${files.length} package JavaScript files follow the mechanical style rules`);
