# Gamma workspace extraction boundary

**Initial slice:** this is an ownership and dependency gate, not a source move.
The transitional `browser/workspace/` package still runs from Fawn. Move it only
after Gamma builds and passes the same tests independently.

| Owner | Source boundary |
| --- | --- |
| Fawn | `browser/chromium/`, Chromium overlays, launcher, runtime selection, and native capture implementation. Optional clients bind a browser capability; no Gamma service is started or required by the browser. |
| Doe | Native GPU runtime, shader compiler, public WebGPU packages and bridge. No workspace dependency. |
| Gamma | Move `browser/workspace/src/` as a unit: authority checks, durable journal, dispatch, browser adapters (including the optional Fawn client adapter), graph/recipe engines, UI, and synchronization. Own new native desktop adapters here too. |

## Receiving unit and references

- Bring the workspace's package and lockfile, test runner, tests and fixtures into
  Gamma with production code, separate from research imports. Preserve the current
  package/command identity for the first parity run; retire it after migration.
- Relocate the live service/qualification config and schemas and their contract
  paths from Fawn's root `config/` only after inventorying all readers. Inspect
  `schema-targets.json`, `tool-surfaces.json`, and product strategy config before
  removing entries. Keep historical files and schema/evidence identities where
  frozen receipts still refer to them.
- Move Gamma-owned workflow qualification and deploy manifests; leave Fawn/Doe
  execution and integrated comparison harnesses on the Fawn side. Their optional
  workspace runner should take a pinned artifact as an explicit input. Do not
  relabel old reports, strategy keys or frozen receipts.
- Keep stock Chromium/Chrome selectable without the Fawn fork. Optional native
  capture is a browser capability, not a holder of workspace permissions,
  dispatch, journal or private state.

## Cutover gates

1. Stage the proposed code and run
   `python3 browser/chromium/scripts/check-workspace-independence.py`. The guard
   scans the Git index for direct workspace references in the Chromium, runtime,
   and packages trees; it also works from a sparse checkout. It is not a
   transitive dependency audit or browser smoke.
2. Run Fawn startup, navigation and WebGPU checks, and Doe's standalone smoke
   in clean jobs with no Gamma or legacy workspace installation. Run the pinned
   end-to-end GPU workload with the explicit optional workspace runner separately.
3. In Gamma, run workspace unit and stock Chrome browser tests from the built
   production package without research sources or a Fawn checkout. Compare
   recovery, permission, and replay behavior against the donor.
4. Only then switch package ownership and remove redundant active donor files.
   Do not delete historical evidence.
