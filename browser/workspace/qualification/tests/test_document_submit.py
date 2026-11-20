import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).parents[1] / "correlate-document-submit.py"
spec = importlib.util.spec_from_file_location("document_submit", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
frame, loader, binding, command = [c * 32 for c in "ABCD"]
other_frame, other_binding, other_command = [c * 32 for c in "EF1"]
options = dict(tracing_complete={"dataLossOccurred": False}, browser_pid=8, gpu_pid=9, frame=frame, loader=loader, scope_sha256="c" * 64, doe_sha256="d" * 64)


def instant(name, owner, at, **args):
    return dict(name="FawnResident" + name, pid=owner, ph="I", cat=module.CATEGORY, ts=at, args=args)


def diagnostic():
    browser = [instant("DocumentValidated", 8, 1, binding=binding, frame=frame, loader=loader),
               instant("DocumentValidated", 8, 1, binding=other_binding, frame=other_frame, loader=loader),
               instant("WireSubmit", 9, 2, binding=binding, submission=command),
               instant("WireSubmit", 9, 2, binding=other_binding, submission=other_command)]
    native = [dict(event="collector-armed", pid=9, scopeImageSha256="c" * 64, nativeLibrarySha256="d" * 64)]
    for i, (doc, submit) in enumerate([(binding, command), (other_binding, other_command)], 1):
        native.extend([dict(event="trusted-wire-scope", submit=i, queue=i, documentBinding=doc, wireSubmission=submit),
                       dict(event="native-fence-observed", submit=i, queue=i)])
    native.append(dict(event="collector-ended", exitCode=0, errorCount=0, unresolved=[]))
    return browser, native


class DocumentSubmitTest(unittest.TestCase):
    def test_concurrent_documents_and_no_capture_qualification(self):
        result = module.correlate(*diagnostic(), **options)
        self.assertEqual(result["matchedDocumentSubmissions"], [command])
        self.assertFalse(result["authorizedSampleProven"])
        self.assertFalse(result["latencyQualified"])

    def test_retirement_and_fake_renderer_event(self):
        events, native = diagnostic()
        events.append(instant("DocumentRetired", 8, 3, binding=binding))
        self.assertEqual(module.correlate(events, native, **options)["matchedDocumentSubmissions"], [])
        events.pop()
        events[0]["pid"] = 10
        self.assertEqual(module.correlate(events, native, **options)["matchedDocumentSubmissions"], [])

    def test_missing_or_dropped_trace(self):
        for completion in ({}, {"dataLossOccurred": True}):
            with self.assertRaises(ValueError):
                module.correlate(*diagnostic(), **(options | {"tracing_complete": completion}))

    def test_duplicate_wire_ids_missing_fences_and_prebirth(self):
        events, native = diagnostic()
        with self.assertRaises(ValueError):
            module.correlate(events + [events[2]], native, **options)
        self.assertEqual(module.correlate(events, native[:2] + native[3:], **options)["matchedDocumentSubmissions"], [])
        events[2]["ts"] = 0
        with self.assertRaises(ValueError):
            module.correlate(events, native, **options)

    def test_binding_reuse_and_cross_image_and_native_interruption(self):
        events, native = diagnostic()
        rebound = instant("DocumentValidated", 8, 5, binding=binding, frame=other_frame, loader=loader)
        with self.assertRaises(ValueError):
            module.correlate(events + [rebound], native, **options)
        native[0]["scopeImageSha256"] = "e" * 64
        with self.assertRaises(ValueError):
            module.correlate(events, native, **options)
        events, native = diagnostic()
        native[-1]["unresolved"] = [1]
        with self.assertRaises(ValueError):
            module.correlate(events, native, **options)


if __name__ == "__main__":
    unittest.main()
