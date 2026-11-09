from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_ROOT = REPO_ROOT / "bench"
for _path_entry in (str(REPO_ROOT), str(BENCH_ROOT)):
    if _path_entry not in sys.path:
        sys.path.insert(0, _path_entry)

from bench.lib.bench_utils import (
    backend_from_token,
    extract_backends_from_command,
    infer_backend_from_lane_name,
    load_json_object,
    write_json_object,
)
from bench.native_compare_modules import workload_validation


def _load_run_bench_module():
    script_path = BENCH_ROOT / "single-runtime" / "run_bench.py"
    spec = importlib.util.spec_from_file_location("single_runtime_run_bench", script_path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class BenchUtilsTest(unittest.TestCase):
    def test_write_json_object_uses_canonical_object_format(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "nested" / "payload.json"

            write_json_object(path, {"z": 1, "a": {"b": 2}})

            self.assertEqual(
                path.read_text(encoding="utf-8"),
                '{\n  "a": {\n    "b": 2\n  },\n  "z": 1\n}\n',
            )
            self.assertEqual(load_json_object(path), {"z": 1, "a": {"b": 2}})

    def test_backend_detection_shared_across_compare_and_single_runtime(self) -> None:
        run_bench = _load_run_bench_module()
        self.assertIs(workload_validation.backend_from_token, backend_from_token)
        self.assertIs(run_bench.backend_from_token, backend_from_token)
        self.assertIs(workload_validation.infer_backend_from_lane_name, infer_backend_from_lane_name)
        self.assertIs(run_bench.infer_backend_from_lane_name, infer_backend_from_lane_name)
        self.assertIs(workload_validation.extract_backends_from_command, extract_backends_from_command)
        self.assertIs(run_bench.extract_backends_from_command, extract_backends_from_command)

        for lane, expected in (
            ("doe-dx12", "d3d12"),
            ("dawn-d3d12", "d3d12"),
            ("windows-direct3d12-lane", "d3d12"),
            ("linux-vulkan-radv", "vulkan"),
            ("macos-metal-default", "metal"),
            ("unknown-lane", None),
        ):
            self.assertEqual(workload_validation.infer_backend_from_lane_name(lane), expected)
            self.assertEqual(run_bench.infer_backend_from_lane_name(lane), expected)

        for token, expected in (
            ("dx12", "d3d12"),
            ("direct3d12", "d3d12"),
            ("d3d12", "d3d12"),
            ("vulkan", "vulkan"),
            ("metal", "metal"),
            ("webgpu", "webgpu"),
            ("opengl", None),
        ):
            self.assertEqual(workload_validation.backend_from_token(token), expected)
            self.assertEqual(run_bench.backend_from_token(token), expected)

        command_cases = [
            (["runner", "--backend-lane", "doe-dx12"], {"d3d12"}),
            (["runner", "--backend-lane=doe-dx12"], {"d3d12"}),
            (["runner", "--backend", "dx12", "--api=vulkan"], {"d3d12", "vulkan"}),
            (["runner", "--dawn-extra-args", "--backend=metal"], {"metal"}),
            (["runner", "--dawn-extra-args=--backend", "direct3d12"], {"d3d12"}),
        ]
        for args, expected_set in command_cases:
            self.assertEqual(workload_validation.extract_backends_from_command(args), expected_set)
            self.assertEqual(run_bench.extract_backends_from_command(args), expected_set)


if __name__ == "__main__":
    unittest.main()
