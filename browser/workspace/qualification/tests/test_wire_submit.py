import copy
from pathlib import Path
import runpy
import unittest

QUAL = Path(__file__).resolve().parents[1]
wire = runpy.run_path(str(QUAL / "check-wire-submit.py"))
mask_fixture = runpy.run_path(str(Path(__file__).with_name("test_mask_readback.py")))["fixture"]


def fixture():
    events, scope, _ = mask_fixture()
    _, command = wire["MASK"]["traced_mask"](events, scope, 9)

    def event(name, pid, ts, **args):
        return dict(
            name="FawnResident" + name,
            cat=wire["INPUT"]["CATEGORY"],
            ph="i",
            pid=pid,
            tid=12,
            ts=ts,
            args=args,
        )

    note = dict(binding="A" * 32, submission="C" * 32)
    client = event(
        "EncodedSubmit",
        10,
        3.37,
        context=3,
        device=7,
        command=76,
        batch="18446744073709551614",
        offset=144,
    )
    gpu = event("DecodedSubmit", 14, 3.38, batch=client["args"]["batch"], offset=144, **note)
    doc = event(
        "DocumentValidated",
        9,
        2.75,
        rendererPid=10,
        frame=scope["frameId"],
        loader=scope["loaderId"],
        binding=note["binding"],
    )
    retired = event("DocumentRetired", 9, 9, binding=note["binding"])
    # Another real GPU proc in the SAME normal batch and another unrelated device.
    earlier = copy.deepcopy(gpu)
    earlier["args"].update(offset=8, submission="D" * 32)
    extra_client = copy.deepcopy(client)
    extra_client["args"].update(offset=8, command=64, device=5)
    events.extend((client, gpu, doc, retired, earlier, extra_client))
    return events, scope, command, (client, gpu, doc, retired, earlier, extra_client)


class OrdinaryWireSubmitTest(unittest.TestCase):
    def test_mixed_normal_batch_extracts_one_real_instruction(self):
        events, scope, command, _ = fixture()
        verdict = wire["wire_submit"](events, scope, 9, 14, command)
        self.assertTrue(verdict["nativeQueueSubmitInvoked"])
        self.assertEqual((verdict["wireOffset"], verdict["submission"]), (144, "C" * 32))
        self.assertFalse(verdict["nativeBackendCompletionVerified"])
        self.assertFalse(verdict["latencyQualified"])

    def test_neighbouring_work_and_invalid_coordinates_are_insufficient(self):
        mutations = [
            ("client device", 0, "device", 1),
            ("client command", 0, "command", 64),
            ("client world", 0, "context", 10),
            ("wrong header", 1, "offset", 8),
            ("wrong batch", 1, "batch", "9"),
            ("zero batch", 0, "batch", "0"),
            ("copied submit token", 4, "submission", "C" * 32),
            ("another command at offset", 5, "offset", 144),
            ("unknown binding", 1, "binding", "E" * 32),
            ("other browser doc", 2, "loader", "B" * 32),
            ("other renderer", 2, "rendererPid", 33),
            ("no addresses", 1, "submission", "0xf007"),
        ]
        for name, i, key, value in mutations:
            with self.subTest(name):
                events, scope, command, native = fixture()
                native[i]["args"][key] = value
                with self.assertRaises(ValueError):
                    wire["wire_submit"](events, scope, 9, 14, command)
        for name in (
            "wrong GPU PID",
            "duplicate",
            "page spoof",
            "retired",
            "outside capture",
            "extra birth",
            "missing",
            "wrong renderer thread",
            "boolean offset",
            "repeated command",
        ):
            with self.subTest(name):
                events, scope, command, (client, gpu, doc, retired, earlier, other) = fixture()
                if name == "wrong GPU PID":
                    gpu["pid"] = 13
                if name == "duplicate":
                    events.append(copy.deepcopy(gpu))
                if name == "page spoof":
                    gpu["cat"] = "blink.user_timing"
                if name == "retired":
                    retired["ts"] = 3.36
                if name == "outside capture":
                    gpu["ts"] = command["end"] + 0.5
                if name == "extra birth":
                    events.append(copy.deepcopy(doc))
                if name == "missing":
                    events.remove(gpu)
                if name == "wrong renderer thread":
                    client["tid"] = 14
                if name == "boolean offset":
                    client["args"]["offset"] = 0
                    gpu["args"]["offset"] = False
                if name == "repeated command":
                    other["args"].update(device=7, command=76)
                with self.assertRaises(ValueError):
                    wire["wire_submit"](events, scope, 9, 14, command)


if __name__ == "__main__":
    unittest.main()
