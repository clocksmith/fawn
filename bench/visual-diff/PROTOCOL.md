# Native composited image comparison

Freeze the following diagnostic before collecting times. The primary outcome is
the nearest-rank p95 elapsed time from delivery of two completed image buffers to
availability of their exact changed-tile mask on the CPU. Record p50 and p99,
individual samples, explicit full-frame copies, bytes traversing the CPU/GPU
boundary, result readbacks, hardware, and implementation identity. Include
surface import, synchronization, masks, and any CPU materialization inside that
interval. Report compositor acquisition separately. GPU/device/pipeline creation
is setup. Failed arms and correctness disagreements remain in the report.

Request only a disposable window owned by this experiment. No model or inference
server participates. Capture two distinct frames and retain both surfaces
through all consumers. Use 512 × 256 BGRA8, 32 tiles (64 × 64), and include
equal frames alongside localized differences. An independently known pattern
supplies the expected mask. For 16 warmups and 128 measured samples per arm,
alternate the order of the arms and include both workloads equally. Report the
repeated-frame scope. These samples say nothing about cold frames from a changing
application.

Compare a full per-row CPU readback with a native fused Dawn comparison and
the same kernel on Doe when both can import the producer's native resource.
For a Doe advantage require zero wrong masks, at least 10% lower p95 than both
working baselines, and p99 at most 5% higher than each. A missing arm precludes
that conclusion. Account for explicit host transfers. Shared or unified memory
may undergo additional driver transfers that application counters cannot observe.

Record the surface producer. ScreenCaptureKit supplies a completed, WindowServer-
composited IOSurface; that route does not establish access to Chromium Viz's own
mailbox or its producer fence. Any CDP screenshot baseline includes Chromium's
capture and materialization costs and is reported separately from native timing.
