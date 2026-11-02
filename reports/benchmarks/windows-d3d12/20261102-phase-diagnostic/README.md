# Windows D3D12 native phase diagnostic

The [summary versus Dawn](summary-vs-dawn.json) and
[summary versus the previous C build](summary-vs-previous.json) retain all three
workloads and adverse phases. [Handoff](handoff.json) records raw checksums,
host-window observations and Dawn A/A checks. [Retention](retention.json)
describes evidence limits and the [accepted/rejected host windows](host-window-evidence.tar.gz).

All 288 main processes passed full output checks, totaling 9,216 checks. The
measured C build is `50cf8feb8`, the previous C build is `aa402f004`, and Dawn
is `f9997677`. These are single-flight, 32-iteration processes, including 31
warm iterations. Dawn A/A and the retained foreign-load gates pass on the
accepted windows. A rejected image prewindow is retained separately; it ran
no image process. Missing own-process counter instances remain explicit.

All three Doe/Dawn warm-operation intervals lie above one: these are diagnostic
losses, with a large completion-delay mode. Driver and freeze date remain
unknown in their respective receipts. This evidence cannot establish performance
for a later binary, another host, browser integration or a release qualification.
