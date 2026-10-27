"""Independent CPU references and fail-closed full-buffer admission controls."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import jsonschema

from bench.tools.prepare_package_oracle_plans import image_reference, prepare, vector_reference


ROOT = Path(__file__).resolve().parents[2]
PLAN = (ROOT / "bench/executors/node-webgpu/plan.js").as_uri()


class PackageOraclePlansTests(unittest.TestCase):
    def test_cpu_reference_preserves_only_split_and_fused_results(self) -> None:
        reference = vector_reference(struct.pack("<2f", 0.10668247193098068, -0.08048912137746811))
        self.assertEqual(reference["values"], [
            [0.41002368927001953, 0.4100237190723419], [0.12926632165908813]])
        with self.assertRaises(ValueError):
            vector_reference(struct.pack("<f", float("nan")))
        with self.assertRaises(ValueError):
            vector_reference(struct.pack("<f", 0.5))

    def test_image_reference_exact_xor_preserves_alpha(self) -> None:
        self.assertEqual(image_reference(bytes.fromhex("123456aaffff0000")),
                         bytes.fromhex("edcba9aa0000ff00"))

    def test_validator_checks_full_span_and_explicit_choices(self) -> None:
        script = f"""
import {{validateSampleExpectation, normalizePlan}} from {json.dumps(PLAN)};
const expectation = {{kind:'f32BufferOneOfV1', values:[[0.41002368927001953,0.4100237190723419],[0.25]]}};
function check(values, offset=0) {{
  const data = new Uint8Array(values.length * 4 + offset);
  const view = new DataView(data.buffer);
  values.forEach((v,i)=>view.setFloat32(offset+i*4,v,true));
  return validateSampleExpectation(data.subarray(offset),expectation).ok;
}}
function normalize(validate) {{
  try {{ normalizePlan({{schemaVersion:1,planId:'test',executorId:'node_webgpu_package',workloadId:'test',domain:'compute',comparable:true,
    timing:{{iterations:1,warmup:0,timingSource:'test',timingClass:'operation'}},
    buffers:[{{id:'result',size:8,usage:['map_read']}}],modules:[],
    steps:[{{kind:'readBuffer',bufferId:'result',validate}}]}}); return true; }} catch {{ return false; }}
}}
console.log(JSON.stringify({{
  split:check([0.41002368927001953,0.25]),fused:check([0.4100237190723419,0.25],1),
  unlisted:check([0.4100237488746643,0.25]),lastWrong:check([0.4100237190723419,0.5]),
  truncated:check([0.4100237190723419]),extra:check([0.4100237190723419,0.25,0.25]),
  nan:check([NaN,0.25]),infinite:check([Infinity,0.25]),
  normalization:normalize(expectation),invalid:[[],[[]],[[NaN]],[[0.1]],[[0.25,0.25]],[[0.25,0.5,0.75]]].map(values=>normalize({{kind:'f32BufferOneOfV1',values}})),
  oldStrict:validateSampleExpectation(new Uint8Array(new Float32Array([0.4100237190723419]).buffer),{{kind:'f32PrefixEquals',values:[0.41002368927001953]}}).ok
}}));
"""
        result = subprocess.run(["node", "--input-type=module", "-e", script],
                                cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        controls = json.loads(result.stdout)
        self.assertTrue(controls.pop("split"))
        self.assertTrue(controls.pop("fused"))
        self.assertTrue(controls.pop("normalization"))
        self.assertEqual(controls.pop("invalid"), [False] * 6)
        self.assertTrue(all(value is False for value in controls.values()), controls)

    def test_schema_versions_new_kind_without_extra_tolerances(self) -> None:
        schema = json.loads((ROOT / "bench/plans/package_readback_expectation.schema.json").read_text())
        good = {"kind": "f32BufferOneOfV1", "values": [[0.25], [0.5, 0.75]]}
        jsonschema.validate(good, schema)
        for bad in [{**good, "epsilon": 1}, {**good, "values": []},
                    {**good, "values": [[0.25, 0.5, 0.75]]},
                    {**good, "kind": "f32PrefixEquals"}]:
            with self.assertRaises(jsonschema.ValidationError):
                jsonschema.validate(bad, schema)

    def test_prospective_plans_cover_all_output_without_changing_shaders(self) -> None:
        with tempfile.TemporaryDirectory() as scratch:
            manifest = prepare(Path(scratch) / "plans")
            self.assertEqual([row["fullOutputBytes"] for row in manifest["plans"]],
                             [1048576, 4194304])
            for row in manifest["plans"]:
                plan = json.loads(Path(row["plan"]).read_text())
                original = json.loads((ROOT / "bench/plans/package-developer" /
                                       (row["workloadId"] + ".plan.json")).read_text())
                self.assertEqual(plan["commands"][:2], original["commands"][:2])
                dispatch = plan["commands"][-1]
                for key, value in original["commands"][-1].items():
                    if key != "captureSize":
                        self.assertEqual(dispatch[key], value)
                self.assertEqual(dispatch["captureSize"], row["fullOutputBytes"])
                if row["referenceKind"] == "f32BufferOneOfV1":
                    self.assertEqual(len(dispatch["captureValidate"]["values"]), 262144)
            with self.assertRaises(FileExistsError):
                prepare(Path(scratch) / "plans")


if __name__ == "__main__":
    unittest.main()
