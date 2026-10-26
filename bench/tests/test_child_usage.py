"""Exercise real owned-process CPU and Windows working-set observations."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest

from bench.lib.child_usage import child_cpu_snapshot, completed_child_cpu_ms, windows_child_usage


class ChildUsageTests(unittest.TestCase):
    def test_observes_owned_cpu_child_after_wait(self) -> None:
        before = child_cpu_snapshot()
        script = (
            "import time; data=bytearray(16*1024*1024); start=time.process_time(); "
            "exec('while time.process_time()-start < 0.2: pass'); time.sleep(0.02)"
        )
        with subprocess.Popen([sys.executable, "-c", script]) as process:
            process.wait(timeout=15)
            self.assertEqual(process.returncode, 0)
            self.assertGreater(completed_child_cpu_ms(process, before), 100.0)
            if os.name == "nt":
                cpu_ms, peak_kib = windows_child_usage(process)
                self.assertGreater(cpu_ms, 100.0)
                self.assertGreater(peak_kib, 16 * 1024)

    def test_rejects_cpu_completion_before_child_stops(self) -> None:
        before = child_cpu_snapshot()
        with subprocess.Popen([sys.executable, "-c", "import time; time.sleep(10)"]) as process:
            try:
                with self.assertRaises(ValueError):
                    completed_child_cpu_ms(process, before)
            finally:
                process.kill()
                process.wait(timeout=15)


if __name__ == "__main__":
    unittest.main()
