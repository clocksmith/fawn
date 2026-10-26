"""A later lifecycle fault preserves completed smoke work and still fails strict mode."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "browser/chromium/scripts/webgpu-playwright-smoke.mjs"


class BrowserSmokeLifecycleTests(unittest.TestCase):
    def test_lifecycle_failure_keeps_completed_suite_and_fails(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fawn-smoke-lifecycle-") as directory:
            root = Path(directory)
            browser = root / "browser"
            browser.write_bytes(b"test browser identity")
            fake = root / "playwright.mjs"
            fake.write_text("""
const pass = { pass: true };
const suite = {
  webgpuAvailable: true, adapterAvailable: true,
  adapterInfo: { vendor: 'test', architecture: 'test', device: 'test', description: 'test' },
  features: [], limits: {}, wgslLanguageFeatures: [], errors: [],
  smoke: Object.fromEntries(['computeIncrement', 'renderTriangle', 'renderBundle',
    'renderIndirect', 'timestampQuery', 'requestAdapterXrCompatible',
    'copyExternalImageToTexture', 'importExternalTexture'].map(name => [name, pass])),
  webgpuCanvasApi: { webgpuContextAvailable: true },
  benches: { writeBuffer64kbUsPerOp: 1, computeDispatchUsPerOp: 1 },
};
export const chromium = {
  async launch() {
    return {
      version() { return 'test-browser'; },
      async newContext() {
        return {
          async newPage() {
            return {
              async goto() {},
              async evaluate() { return structuredClone(suite); },
              async waitForFunction() { throw new Error('injected lifecycle fault'); },
            };
          },
          async close() {},
        };
      },
      async close() {},
    };
  },
};
""", encoding="utf-8")
            loader = root / "loader.mjs"
            loader.write_text(
                "export async function resolve(name, context, next) {\n"
                f"  if (name === 'playwright') return {{ url: {json.dumps(fake.as_uri())}, shortCircuit: true }};\n"
                "  return next(name, context);\n}\n", encoding="utf-8")
            report = root / "report.json"
            result = subprocess.run([
                "node", "--experimental-loader", str(loader), str(RUNNER),
                "--chrome", str(browser), "--mode", "dawn", "--strict",
                "--sandbox", "true", "--out", str(report),
            ], cwd=ROOT, capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            row = json.loads(report.read_text(encoding="utf-8"))["modeResults"][0]
            self.assertTrue(row["webgpuAvailable"])
            self.assertTrue(row["adapterAvailable"])
            self.assertTrue(row["smoke"]["computeIncrement"]["pass"])
            self.assertTrue(row["activeRuntimeProof"]["matched"])
            self.assertFalse(row["fawnPrismaticLifecycle"]["pass"])
            self.assertIn("injected lifecycle fault", row["fawnPrismaticLifecycle"]["error"])
            self.assertIn("injected lifecycle fault", row["errors"][0])


if __name__ == "__main__":
    unittest.main()
