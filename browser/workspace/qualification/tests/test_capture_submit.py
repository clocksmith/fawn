import copy
import unittest

from test_document_submit import (
    module,
    options,
    diagnostic,
    instant,
    frame,
    loader,
    command,
)

STREAM = "2" * 40
TARGET = "3" * 40
WORLD = 17


def records():
    events, native = diagnostic()
    events[0]["args"]["rendererPid"] = 10
    events.extend(
        [
            instant(
                "TrackRestriction",
                10,
                5,
                target=TARGET,
                stream=STREAM,
                version=1,
                track="4" * 32,
                context=WORLD,
            ),
            instant(
                "BrowserRestriction",
                8,
                6,
                target=TARGET,
                stream=STREAM,
                version=1,
                accepted=True,
                restriction=True,
            ),
        ]
    )
    for at in (10, 25):
        events.append(
            instant(
                "TrackState",
                10,
                at,
                stream=STREAM,
                track="4" * 32,
                context=WORLD,
                live=True,
            )
        )
    events.extend(
        [
            dict(instant("WorldSubmit", 10, 16, context=WORLD), ph="X", tid=101, dur=3),
            dict(instant("RendererBatch", 10, 18, batch="123"), tid=101),
            instant("GpuBatch", 9, 20, batch="123", submission=command),
        ]
    )
    # The page can use the same event/command name in its own world and trace
    # category. A concurrent stream in another context is equally unrelated.
    events.extend(
        [
            dict(
                instant("WorldSubmit", 10, 16, context=WORLD),
                ph="X",
                tid=101,
                dur=3,
                cat="blink.user_timing",
            ),
            instant(
                "TrackRestriction",
                10,
                16,
                version=1,
                stream="5" * 40,
                target=TARGET,
                track="6" * 32,
                context=18,
            ),
        ]
    )
    request = dict(
        frameId=frame,
        loaderId=loader,
        executionContextId=WORLD,
        captureId="test-owned-session",
        grantId="test-surface",
        grantRevision=7,
        bracket=1,
    )
    return events, native, request


def qualify(events, native, request):
    doc = module.correlate(events, native, **options)
    return module.capture_submission(
        events,
        doc["matchedDocumentSubmissions"],
        request,
        browser_pid=8,
        gpu_pid=9,
        frame=frame,
        loader=loader,
    )


class CaptureSubmitTest(unittest.TestCase):
    def test_exact_track_world_batch_and_native_fence(self):
        self.assertEqual(qualify(*records()), command)

    def test_lookalike_world_or_two_tracks_in_same_world(self):
        events, native, req = records()
        for change in ("stream", "track", "live", "context"):
            with self.subTest(change):
                altered = copy.deepcopy(events)
                r = next(
                    e
                    for e in altered
                    if e["name"] == "FawnResidentTrackState" and e["ts"] == 25
                )
                r["args"][change] = (
                    19
                    if change == "context"
                    else (
                        False
                        if change == "live"
                        else "7" * (32 if change == "track" else 40)
                    )
                )
                with self.assertRaises(ValueError):
                    qualify(altered, native, req)
        with self.assertRaises(ValueError):
            qualify(events, native, req | {"executionContextId": 18})

    def test_stop_reject_or_switch_mid_dispatch(self):
        events, native, req = records()
        with self.assertRaises(ValueError):
            qualify(
                events + [instant("StreamClosed", 8, 22, stream=STREAM)], native, req
            )
        self.assertEqual(
            qualify(
                events + [instant("StreamClosed", 8, 21, stream="7" * 40)], native, req
            ),
            command,
        )
        with self.assertRaises(ValueError):
            qualify(
                events
                + [
                    instant(
                        "BrowserRestriction",
                        8,
                        21,
                        stream=STREAM,
                        target="0" * 40,
                        version=8,
                        restriction=False,
                        accepted=True,
                    )
                ],
                native,
                req,
            )
        next(e for e in events if e["name"] == "FawnResidentBrowserRestriction")[
            "args"
        ]["accepted"] = False
        with self.assertRaises(ValueError):
            qualify(events, native, req)

    def test_extra_commands_and_batches_wrong_thread_and_copied_labels(self):
        events, native, req = records()
        world = next(e for e in events if e["name"] == "FawnResidentWorldSubmit")
        with self.assertRaises(ValueError):
            qualify(events + [world], native, req)
        with self.assertRaises(ValueError):
            qualify(
                events + [dict(instant("RendererBatch", 10, 18, batch="124"), tid=101)],
                native,
                req,
            )
        batch = next(e for e in events if e["name"] == "FawnResidentRendererBatch")
        batch["tid"] = 103
        with self.assertRaises(ValueError):
            qualify(events, native, req)
        batch["tid"] = 101
        world["cat"] = "blink.user_timing"
        world["args"]["label"] = "blitzen-surface/copied/sample-1/diff"
        with self.assertRaises(ValueError):
            qualify(events, native, req)

    def test_no_fence_and_wrong_renderer(self):
        events, native, req = records()
        with self.assertRaises(ValueError):
            qualify(events, native[:2] + native[3:], req)
        events[0]["args"]["rendererPid"] = 12
        with self.assertRaises(ValueError):
            qualify(events, native, req)
