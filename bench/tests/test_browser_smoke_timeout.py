"""Execute the real smoke callback with CPU-only pending GPU-interface controls."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "browser/chromium/scripts/webgpu-playwright-smoke.mjs"


class BrowserSmokeTimeoutTests(unittest.TestCase):
    def run_scenario(self, scenario: str) -> tuple[dict, list[str]]:
        with tempfile.TemporaryDirectory(prefix="fawn-smoke-timeout-") as directory:
            root = Path(directory)
            image = root / "identity"
            image.write_bytes(b"CPU test identity; not a browser or C runtime")
            calls = root / "calls.json"
            fake = root / "playwright.mjs"
            fake.write_text(
                "import {writeFileSync} from 'node:fs';\n"
                f"const scenario={json.dumps(scenario)}; const callFile={json.dumps(str(calls))};\n"
                + r"""
const calls=[];
const log=name=>{calls.push(name);writeFileSync(callFile,JSON.stringify(calls));};
const pending=()=>new Promise(()=>{});
const makeQueue=()=>({writeBuffer(){log('write');},submit(){log('submit');},onSubmittedWorkDone(){
  log('workdone-wait');
  if(scenario==='queue-reject')return Promise.reject(Error('submitted completion rejected'));
  if(scenario==='queue-pending')return pending();
  return Promise.resolve();
}});
const device={
  lost:pending(), addEventListener(){},
  queue:makeQueue(),
  createBuffer(){return {
    mapAsync(){log('submitted-readback-wait');
      if(scenario==='readback-reject')return Promise.reject(Error('submitted readback rejected'));
      if(scenario==='submitted-readback')return pending();
      return Promise.resolve();
    },getMappedRange(){return new Uint32Array([2,3,4,5]).buffer;},unmap(){},destroy(){},
  };},
  createShaderModule(){return {async getCompilationInfo(){return {messages:[{type:'error'}]};}};},
  createComputePipeline(){return {getBindGroupLayout(){return {};}};},
  createBindGroup(){return {};},
  createCommandEncoder(){return {
    beginComputePass(){return {setPipeline(){},setBindGroup(){},dispatchWorkgroups(){log('dispatch');},end(){}};},
    copyBufferToBuffer(){},finish(){return {};},
  };},
  createTexture(){log('later-render-case');throw Error('must not reach later GPU case');},
};
const adapter={features:new Set(),limits:{},info:{vendor:'CPU-control'},async requestDevice(){
  log('requestDevice');return {...device,queue:makeQueue()};
}};
const gpu={
  requestAdapter(){log('requestAdapter');
    if(scenario==='adapter')return pending();
    if(scenario==='known-rejection')return Promise.reject(Error('known adapter rejection'));
    return Promise.resolve(adapter);
  }, getPreferredCanvasFormat(){return 'rgba8unorm';},wgslLanguageFeatures:new Set(),
};
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{gpu,userAgent:'CPU-only timeout control'}});
globalThis.GPUBufferUsage={STORAGE:1,COPY_SRC:2,COPY_DST:4,MAP_READ:8};
globalThis.GPUMapMode={READ:1};
const pass={pass:true};
const successfulSuite={gpuCompletion:'completed',webgpuAvailable:true,adapterAvailable:true,
  adapterInfo:{vendor:'CPU-control'},features:[],limits:{},wgslLanguageFeatures:[],errors:[],
  smoke:Object.fromEntries(['computeIncrement','renderTriangle','renderBundle','renderIndirect','timestampQuery',
    'requestAdapterXrCompatible','copyExternalImageToTexture','importExternalTexture'].map(x=>[x,pass])),
  webgpuCanvasApi:{webgpuContextAvailable:true},benches:{writeBuffer64kbUsPerOp:1,computeDispatchUsPerOp:1}};
export const chromium={async launch(options){
  if(!options.ignoreDefaultArgs?.includes('--enable-unsafe-swiftshader'))throw Error('unsafe fallback default was not excluded');
  if(options.chromiumSandbox!==true)throw Error('sandbox must be enabled by default');
  if(options.args.some(arg=>/^--(?:ignore-gpu-blocklist|enable-unsafe-webgpu|unsafely-treat-insecure-origin-as-secure|use-angle|no-sandbox|disable-setuid-sandbox)(?:=|$)/.test(arg)))throw Error('unsupported implicit launch override');
  const external=options.args.includes('--use-webgpu-runtime=doe');
  if(options.args.includes('--gpu-sandbox-start-early')!==(process.platform==='linux'&&external))throw Error('external early-sandbox ordering mismatch');
  log('launch-policy-checked');log('launch');let contexts=0;return {
  version(){return 'CPU-only-control';},
  async newContext(){log('context');const lifecycle=++contexts>1;return {async addInitScript(){},async newPage(){return {
    async goto(){},on(){},async setViewportSize(){},async evaluate(callback,args){
      if(lifecycle)return {adapterRequestCount:2,contextAvailable:true};
      log('evaluate-core');return scenario==='close-failure'?structuredClone(successfulSuite):callback(args);
    },
    async waitForFunction(){log('lifecycle');
      if(['settled','close-failure'].includes(scenario))return;
      throw Error('lifecycle must not be dispatched');
    },
  };},async close(){}};},
  async close(){log('browser-close');if(scenario==='close-failure')throw Error('owned browser close failed');},
};}};
""", encoding="utf-8")
            loader = root / "loader.mjs"
            loader.write_text(
                "export async function resolve(name,context,next){\n"
                f"if(name==='playwright')return {{url:{json.dumps(fake.as_uri())},shortCircuit:true}};\n"
                "return next(name,context);}\n", encoding="utf-8")
            report = root / "report.json"
            process = subprocess.run([
                "node", "--experimental-loader", str(loader), str(RUNNER),
                "--chrome", str(image), "--doe-lib", str(image), "--mode", "both",
                "--strict", "--op-timeout-ms", "10",
                "--suite-timeout-ms", "1000", "--upload-iters", "1", "--dispatch-iters", "1", "--out", str(report),
            ], cwd=ROOT, capture_output=True, text=True, timeout=10, check=False)
            self.assertEqual(process.returncode, 1, process.stdout + process.stderr)
            observed = json.loads(calls.read_text())
            self.assertIn("launch-policy-checked", observed)
            return json.loads(report.read_text(encoding="utf-8")), observed

    def assert_unknown_stops_every_layer(self, report: dict, calls: list[str]) -> dict:
        self.assertEqual(len(report["modeResults"]), 1, "a later provider must never launch")
        row = report["modeResults"][0]
        self.assertEqual(row["gpuCompletion"], "unknown")
        self.assertTrue(row["browserClose"]["pass"], "cleanup success must not erase unknown")
        self.assertTrue(row["fawnPrismaticLifecycle"]["skipped"])
        self.assertFalse(row["fawnPrismaticLifecycle"]["pass"])
        self.assertEqual(calls.count("launch"), 1)
        self.assertEqual(calls.count("context"), 1, "lifecycle must not create another context")
        self.assertEqual(calls.count("browser-close"), 1)
        self.assertNotIn("later-render-case", calls)
        self.assertNotIn("lifecycle", calls)
        return row

    def test_never_settling_adapter_stops_cases_lifecycle_and_provider(self) -> None:
        report, calls = self.run_scenario("adapter")
        row = self.assert_unknown_stops_every_layer(report, calls)
        self.assertEqual(row["firstUnknownOperation"], "requestAdapter xrCompatible")
        self.assertEqual(calls.count("requestAdapter"), 1)
        self.assertNotIn("submit", calls)
        self.assertIn("timed out", row["errors"][0])
        self.assertIn("timed out", row["smoke"]["requestAdapterXrCompatible"]["error"])

    def test_submitted_readback_never_completes_preserves_earlier_results(self) -> None:
        report, calls = self.run_scenario("submitted-readback")
        row = self.assert_unknown_stops_every_layer(report, calls)
        self.assertEqual(calls.count("submit"), 1)
        self.assertEqual(calls.count("submitted-readback-wait"), 1)
        self.assertEqual(row["firstUnknownOperation"], "computeIncrement readback mapAsync")
        self.assertTrue(row["smoke"]["requestAdapterXrCompatible"]["pass"])
        self.assertTrue(row["smoke"]["recovery"]["validationError"]["pass"])
        self.assertIn("computeIncrement readback", row["errors"][0])
        self.assertIn("timed out", row["smoke"]["computeIncrement"]["error"])

    def test_submitted_readback_rejection_is_unknown_and_stops_followups(self) -> None:
        report, calls = self.run_scenario("readback-reject")
        row = self.assert_unknown_stops_every_layer(report, calls)
        self.assertEqual(calls.count("submit"), 1)
        self.assertIn("submitted readback rejected", row["errors"][0])
        self.assertEqual(row["queueCompletions"][0]["workEpoch"], 2)
        self.assertEqual(row["queueCompletions"][0]["settledEpoch"], 0)

    def test_queue_completion_rejection_stops_after_successful_map(self) -> None:
        report, calls = self.run_scenario("queue-reject")
        row = self.assert_unknown_stops_every_layer(report, calls)
        self.assertEqual(calls.count("submit"), 1)
        self.assertEqual(calls.count("workdone-wait"), 1)
        self.assertIn("submitted completion rejected", row["errors"][0])
        self.assertEqual(row["firstUnknownOperation"], "queue primary onSubmittedWorkDone")
        self.assertNotEqual(row["queueCompletions"][0]["workEpoch"], row["queueCompletions"][0]["settledEpoch"])

    def test_queue_completion_never_settles_stops_after_successful_map(self) -> None:
        report, calls = self.run_scenario("queue-pending")
        row = self.assert_unknown_stops_every_layer(report, calls)
        self.assertEqual(calls.count("workdone-wait"), 1)
        self.assertEqual(row["firstUnknownOperation"], "queue primary onSubmittedWorkDone")

    def test_known_rejection_is_recorded_without_becoming_unknown(self) -> None:
        report, calls = self.run_scenario("known-rejection")
        self.assertEqual(calls.count("launch"), 2)
        self.assertEqual(len(report["modeResults"]), 2)
        for row in report["modeResults"]:
            self.assertEqual(row["gpuCompletion"], "not-dispatched")
            self.assertIn("known adapter rejection", row["smoke"]["requestAdapterXrCompatible"]["error"])
            self.assertIn("known adapter rejection", row["errors"][0])

    def test_every_used_owned_queue_has_explicit_settled_epoch(self) -> None:
        report, calls = self.run_scenario("settled")
        self.assertEqual(calls.count("launch"), 2)
        for row in report["modeResults"]:
            self.assertEqual(row["gpuCompletion"], "completed")
            self.assertTrue(row["smoke"]["computeIncrement"]["pass"])
            self.assertEqual(len(row["queueCompletions"]), 2)
            for queue in row["queueCompletions"]:
                self.assertGreater(queue["workEpoch"], 0)
                self.assertEqual(queue["settledEpoch"], queue["workEpoch"])
                self.assertGreater(queue["completionWaits"], 0)

    def test_browser_close_failure_cannot_pass_or_launch_next_provider(self) -> None:
        report, calls = self.run_scenario("close-failure")
        self.assertEqual(len(report["modeResults"]), 1)
        self.assertEqual(calls.count("launch"), 1)
        row = report["modeResults"][0]
        self.assertTrue(row["smoke"]["computeIncrement"]["pass"])
        self.assertTrue(row["fawnPrismaticLifecycle"]["pass"])
        self.assertFalse(row["browserClose"]["pass"])
        self.assertEqual(row["gpuCompletion"], "unknown")


if __name__ == "__main__":
    unittest.main()
