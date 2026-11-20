# A Resident Process handoff with fresh browser evidence

Two Blitzen services have separate state directories, browser profiles, login credentials and journals. The fixture website authenticates writes with independent HTTP-only session cookies and verifies the account and revision at commit. The sender exports a learned read-only filter and two selected evidence references with the standard handoff CLI. The receiver imports them through its own authenticated local service. The portable file contains no source session, frame, cookie, workspace credential, write target or source action ID.

The recipient opens a capture, then its actual Blitzen Node process is killed with SIGKILL. The test checks its Chromium processes have exited. After reopening the journal, the recipient replays the filter with its own input and local permission. It cannot use the sender's administrator credential, the sender's capture, or its own retired capture. With fresh permission it captures the selected canvas and reproduces one evidence digest from the received file. Comparison uses the recipient's fresh pixels. It requires no sender-side lookup.

A repaint changes all 32 canonical 64 × 64 tiles without changing the HTML or Blitzen's DOM content hash. A read grant cannot submit. With a separate submission grant the review of the old image issues zero HTTP POSTs. A fresh review commits one revision under the recipient's account. The fixture discards the browser acknowledgment after the site records that revision. Blitzen reads the site's account-scoped record and marks the effect present. A second reconciliation is refused and the recorded outcome stays settled. The sender then makes a separate, deliberately unacknowledged write under its own account. A second export contains only its action kind and a hashed reference. The recipient cannot check that action or use the routine from the unresolved export. The site received one POST per account across both controls.

Run from `browser/workspace` on a disposable visible desktop with stock Chrome, the package's Playwright dependency and permission to share each current fixture tab.

```sh
BLITZEN_TEST_HANDOFF_CAPTURE=1 BLITZEN_TEST_COMPOSITOR_DIGEST=webgpu \
  node test/browser/portable-handoff.test.js
```

Set `BLITZEN_HANDOFF_RECEIPT` to a new file to retain the measured JSON. The original headless browser handoff and the portable handoff, compositor session, handoff history and handoff resume unit tests also passed.

The recorded run used stock Chromium 158.0.8108.0 with SwiftShader, one low-priority CPU core, network isolation, a private X display and no physical GPU. The browser's internal sandbox was disabled inside the outer isolation and the fixture selected its own tab automatically. This establishes functionality across two local services. External accounts, manual consent, remote handoff transport, native GPU performance and crash recovery on arbitrary websites require separate evidence. All planning in this test was deterministic. No paid model calls were made.
