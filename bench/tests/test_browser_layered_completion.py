"""Exercise real layered control flow with CPU-only Playwright/WebGPU objects."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "browser/chromium/scripts/webgpu-playwright-layered-bench.mjs"


class BrowserLayeredCompletionTests(unittest.TestCase):
    def run_control(self, case: str) -> tuple[dict, list[str], list[dict]]:
        with tempfile.TemporaryDirectory(prefix="fawn-layered-completion-") as directory:
            root = Path(directory)
            image = root / "identity"
            image.write_bytes(b"CPU fixture identity; not a browser/runtime")
            calls = root / "calls.json"
            report = root / "report.json"
            projection = root / "projection.json"
            projection.write_text(json.dumps({
                "schemaVersion": 7, "sourceWorkloadsPath": "CPU-control",
                "sourceWorkloadsSha256": "a" * 64, "rulesPath": "CPU-control",
                "rulesSha256": "b" * 64, "projectionContractHash": "c" * 64,
                "generatedAt": "CPU-control", "rows": [
                    {"sourceWorkloadId": f"row-{i}", "sourceWorkloadName": f"row-{i}",
                     "domain": "startup", "projectionClass": "medium", "layerTarget": "l1",
                     "scenarioTemplate": "startup_adapter_device" if i != 2 else "queue_submit_burst",
                     "comparabilityExpectation": "component", "requiredStatus": "ok",
                     "claimScope": "l1_component_only", "claimLanguage": "CPU control",
                     "projectionNote": "CPU control", "browserWorkload": {
                         "sourceComparable": False, "sourceClaimEligible": False}}
                    for i in range(1, 4)]}))
            workflows = root / "workflows.json"
            workflows.write_text(json.dumps({"schemaVersion": 3,
                "promotionGateRequiredApprovals": ["CPU-control"], "rows": [{
                    "id": "workflow", "scenarioTemplate": "fawn_visual_resource" if case == "visual-abrupt-exit" else "startup_adapter_device",
                    **({"resourcePath": "browser/chromium/resources/fawn-heavy-particles.html"}
                       if case == "visual-abrupt-exit" else {}),
                    "description": "CPU control", "metrics": [], "required": True,
                    "requiredStatus": "ok", "comparabilityExpectation": "component",
                    "claimScope": "diagnostic", "claimLanguage": "CPU control"}]}))
            fake = root / "playwright.mjs"
            fake.write_text(
                "import {readFileSync,writeFileSync} from 'node:fs';\n"
                f"const caseName={json.dumps(case)}, callFile={json.dumps(str(calls))}, report={json.dumps(str(report))};\n"
                + r"""
const calls=[]; const log=x=>{calls.push(x);writeFileSync(callFile,JSON.stringify(calls));};
const pending=()=>new Promise(()=>{});let deviceNumber=0;let events={};let activePage;
const queueFor=id=>{let waits=0;return {
 submit(){log(`submit:${id}`);if(id===2&&caseName==='abrupt-exit')process.exit(23);},
 onSubmittedWorkDone(){log(`settle:${id}`);waits++;
  if(id===2&&caseName==='queue-pending')return pending();
  if(id===2&&caseName==='queue-reject-once'&&waits===1)return Promise.reject(Error('first owned queue rejected'));
  if(id===2&&caseName==='queue-reject')return Promise.reject(Error('owned queue rejected'));
  return Promise.resolve();
 },
};};
const adapter={features:new Set(),info:{vendor:'CPU-control',architecture:'CPU-control'},
 async requestDevice(){const id=++deviceNumber;log(`device:${id}`);return {
  queue:queueFor(id),
  createCommandEncoder(){return {finish(){return {};},beginComputePass(){return {
   setPipeline(){},dispatchWorkgroups(){},end(){},};}};},
  createShaderModule(){if(id===2&&caseName==='known-validation')throw Error('known pipeline validation rejection');return {};},
  createComputePipeline(){return {};},
 };},
};
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{
 userAgent:'CPU-only layered control',gpu:{requestAdapter:async()=>adapter},
}});
export const chromium={async launch(options){
 if(options.chromiumSandbox!==(caseName!=='sandbox-disabled'))throw Error('sandbox selection changed');
 if(!options.ignoreDefaultArgs.includes('--enable-unsafe-swiftshader'))throw Error('unsafe fallback default retained');
 if(options.args.some(x=>/^--(?:enable-unsafe-webgpu|ignore-gpu-blocklist|use-angle|unsafely-treat-insecure-origin-as-secure)(?:=|$)/.test(x)))throw Error('unsafe implicit override');
 const doe=options.args.includes('--use-webgpu-runtime=doe');
 if(options.args.includes('--gpu-sandbox-start-early')!==(doe&&process.platform==='linux'))throw Error('incorrect early GPU sandbox ordering');
 if(caseName==='sandbox-disabled'&&!options.args.includes('--no-sandbox'))throw Error('explicit disabled diagnostic flag missing');
 log('launch');events={};return {
 on(name,fn){events[name]=fn;},version(){return 'CPU-control';},
 async newContext(){return {async newPage(){let handlers={};let navigations=0;
  activePage={on(name,fn){handlers[name]=fn;},off(name){delete handlers[name];},
   async goto(){log('navigate');if(++navigations===3&&caseName==='navigation-failure')throw Error('page navigation timeout');},
   async evaluate(callback,arg){
    if(arg?.scenarioTemplate){log(`scenario:${arg.scenarioConfig?.sourceWorkloadId??arg.scenarioTemplate}`);
     if(arg.scenarioTemplate==='fawn_visual_resource'&&caseName==='visual-abrupt-exit')process.exit(24);
     if(arg.scenarioTemplate==='queue_submit_burst'&&caseName==='controller-pending')return pending();
     if(arg.scenarioTemplate==='queue_submit_burst'&&caseName==='crash-resolved')handlers.crash?.();
     if(arg.scenarioTemplate==='queue_submit_burst'&&caseName==='crash'){handlers.crash?.();return pending();}
     if(arg.scenarioTemplate==='queue_submit_burst'&&caseName==='disconnect'){events.disconnected?.();throw Error('browser connection closed');}
    }
    return callback(arg);
   },
  };return activePage;}};},
 async close(){
  log('close');
  const records=readFileSync(report+'.checkpoints.jsonl','utf8').trim().split('\n').map(JSON.parse);
  if(!records.some(x=>x.event==='before_browser_close'))throw Error('missing precleanup checkpoint');
  log('precleanup-checkpoint-present');
  if(caseName==='close-failure')throw Error('owned browser close failed');
  if(caseName==='close-pending')return pending();
  events.disconnected?.();
 },
};}};
""", encoding="utf-8")
            loader = root / "loader.mjs"
            loader.write_text("export async function resolve(name,context,next){"
                f"if(name==='playwright')return {{url:{json.dumps(fake.as_uri())},shortCircuit:true}};"
                "return next(name,context);}")
            process = subprocess.run([
                "node", "--experimental-loader", str(loader), str(RUNNER),
                "--chrome", str(image), "--doe-lib", str(image), "--mode", "both",
                "--manifest", str(projection), "--workflows", str(workflows),
                "--iters-workflow", "1", "--scenario-controller-deadline-ms", "15",
                "--out", str(report),
                *(["--sandbox", "false"] if case == "sandbox-disabled" else []),
            ], cwd=ROOT, capture_output=True, text=True, timeout=10, check=False)
            self.assertEqual(process.returncode, (24 if case == "visual-abrupt-exit" else 23) if case in ("abrupt-exit", "visual-abrupt-exit") else (0 if case in ("known-validation", "sandbox-disabled") else 1),
                             process.stdout + process.stderr)
            checkpoints = [json.loads(x) for x in Path(str(report) + ".checkpoints.jsonl").read_text().splitlines()]
            if case in ("abrupt-exit", "visual-abrupt-exit"):
                self.assertFalse(report.exists(), "abrupt exit must not invent a terminal report")
                return {}, json.loads(calls.read_text()), checkpoints
            self.assertTrue(report.exists(), process.stdout + process.stderr)
            return json.loads(report.read_text()), json.loads(calls.read_text()), checkpoints

    def test_explicit_disabled_sandbox_remains_a_declared_diagnostic(self) -> None:
        report, calls, _ = self.run_control("sandbox-disabled")
        self.assertFalse(report["sandbox"])
        self.assertFalse(report["cohortStopped"])
        self.assertEqual(calls.count("launch"), 2)

    def test_sandbox_disabling_overrides_are_rejected_before_launch(self) -> None:
        for flag in ("--no-sandbox", "--no-sandbox=true", "--disable-gpu-sandbox",
                     "--disable-setuid-sandbox", "--disable-namespace-sandbox",
                     "--disable-seccomp-filter-sandbox", "--single-process"):
            with self.subTest(flag=flag):
                result = subprocess.run(["node", str(RUNNER), "--chrome-arg", flag],
                                        cwd=ROOT, capture_output=True, text=True, timeout=5, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("conflicts with sandbox-disabling", result.stderr)

    def test_abrupt_exit_retains_completed_prefix_and_unknown_active_dispatch(self) -> None:
        _, calls, checkpoints = self.run_control("abrupt-exit")
        self.assertIn("submit:2", calls)
        self.assertEqual(calls.count("launch"), 1)
        self.assertNotIn("close", calls)
        last = checkpoints[-1]
        self.assertEqual(last["event"], "before_scenario_dispatch")
        self.assertEqual(last["runtimeEvidence"]["activeOperation"], "l1:row-2")
        self.assertEqual(last["runtimeEvidence"]["gpuCompletion"], "unknown")
        self.assertEqual(last["rows"][0][0], "row-1")
        self.assertEqual(last["rows"][0][1]["status"], "ok")
        self.assertEqual(last["rows"][0][1]["gpuCompletion"], "completed")

        _, visual_calls, visual_checkpoints = self.run_control("visual-abrupt-exit")
        self.assertEqual(visual_calls.count("launch"), 1)
        visual_last = visual_checkpoints[-1]
        self.assertEqual(visual_last["runtimeEvidence"]["activeOperation"], "l2:workflow")
        self.assertEqual(visual_last["runtimeEvidence"]["gpuCompletion"], "unknown")
        resource = "browser/chromium/resources/fawn-heavy-particles.html"
        self.assertEqual(visual_checkpoints[0]["runIdentity"]["visualResourceSha256"],
                         {resource: hashlib.sha256((ROOT / resource).read_bytes()).hexdigest()})

    def test_unknown_queue_or_controller_stops_rows_workflows_and_providers(self) -> None:
        for case in ("queue-pending", "queue-reject", "queue-reject-once", "controller-pending", "crash", "crash-resolved", "disconnect", "navigation-failure"):
            with self.subTest(case=case):
                report, calls, checkpoints = self.run_control(case)
                self.assertTrue(report["cohortStopped"])
                self.assertEqual(calls.count("launch"), 1)
                self.assertNotIn("scenario:row-3", calls)
                self.assertNotIn("scenario:startup_adapter_device", calls)
                rows = report["l1"]["rows"]
                self.assertEqual(rows[0]["runtimes"]["dawn"]["status"], "ok")
                evidence = report["modeRunDetails"][0]["runtimeEvidence"]
                self.assertEqual(evidence["gpuCompletion"], "unknown")
                self.assertEqual(evidence["browserClose"]["status"], "completed")
                self.assertTrue(any(x["event"] == "before_browser_close" and
                                    x["runtimeEvidence"]["gpuCompletion"] == "unknown" for x in checkpoints))

    def test_completed_validation_failure_can_continue_without_becoming_unknown(self) -> None:
        report, calls, _ = self.run_control("known-validation")
        self.assertFalse(report["cohortStopped"])
        self.assertEqual(calls.count("launch"), 2)
        self.assertIn("scenario:row-3", calls)
        failed = report["l1"]["rows"][1]["runtimes"]["dawn"]
        self.assertEqual(failed["status"], "fail")
        self.assertEqual(failed["gpuCompletion"], "completed")
        self.assertIn("known pipeline validation", failed["error"])

    def test_close_failure_stops_next_provider_and_keeps_completed_rows(self) -> None:
        for case in ("close-failure", "close-pending"):
            with self.subTest(case=case):
                report, calls, _ = self.run_control(case)
                self.assertTrue(report["cohortStopped"])
                self.assertEqual(calls.count("launch"), 1)
                self.assertTrue(all(x["runtimes"]["dawn"]["status"] == "ok" for x in report["l1"]["rows"]))
                self.assertEqual(report["modeRunDetails"][0]["runtimeEvidence"]["browserClose"]["status"], "failed")


if __name__ == "__main__":
    unittest.main()
