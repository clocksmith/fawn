"""Execute each real resource RAF owner and diagnostic hook without GPU work."""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "browser/chromium/resources"


class BrowserVisualStopDrainTests(unittest.TestCase):
    def test_stop_joins_inflight_queue_and_preserves_failures_without_new_frame(self) -> None:
        for name in ("fawn-heavy-particles", "fawn-magnetic-fluids", "fawn-prismatic-fluids"):
            with self.subTest(resource=name):
                source = (RESOURCES / (name + ".html")).read_text()
                owned = source[source.index("  function schedule() {"):source.index("  document.addEventListener('visibilitychange'")]
                script = r"""
import vm from 'node:vm';
import assert from 'node:assert/strict';
const owned=OWNED;
for(const failure of [false,true])for(const drainFirst of [false,true]){
 let resolveFrame,rejectFrame,resolveDrain;
 let waits=0,submits=0,scheduled=0,cancelled=0,destroys=0;
 const frameWait=new Promise((resolve,reject)=>{resolveFrame=resolve;rejectFrame=reject;});
 const drainWait=new Promise(resolve=>{resolveDrain=resolve;});
 const context={setTimeout,running:true,paused:false,redrawRequested:true,raf:0,pendingFrames:0,
  MAX_PENDING_FRAMES:2,metricGeneration:0,document:{hidden:false},
  performance:{now:()=>1},resizeCanvas(){},render(){submits++;},fail(){throw Error('must not destroy after stop');},
  cancelAnimationFrame(){cancelled++;},requestAnimationFrame(){scheduled++;return 1;},
  device:{queue:{onSubmittedWorkDone(){return ++waits===1?frameWait:drainWait;}},destroy(){destroys++;}},
 };
 vm.createContext(context);vm.runInContext(owned,context);
 const inflight=context.frame(0);assert.equal(submits,1);
 const before=scheduled;
 const draining=context.__fawnDiagnosticStopAndDrain();
 assert.equal(draining,context.__fawnDiagnosticStopAndDrain(),'one-way idempotent promise');
 assert.equal(context.running,false);assert.equal(context.raf,0);
 let completed=false;draining.then(()=>completed=true,()=>{});
 await Promise.resolve();assert.equal(completed,false,'stop must await actual queue');
 if(drainFirst){resolveDrain();await Promise.resolve();assert.equal(completed,false,'drain must join prior frame waits');}
 if(failure)rejectFrame(Error('first frame completion rejected'));else resolveFrame();
 await inflight;
 if(!drainFirst)resolveDrain();
 if(failure)await assert.rejects(draining,/first frame completion rejected/);
 else assert.equal((await draining).gpuCompletion,'completed');
 assert.equal(scheduled,before,'inflight finalizer must not schedule another RAF');
 assert.equal(submits,1);assert.equal(destroys,0);assert.equal(cancelled,1);
}
""".replace("OWNED", json.dumps(owned))
                result = subprocess.run(["node", "--input-type=module", "-e", script],
                                        cwd=ROOT, capture_output=True, text=True, timeout=5, check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
