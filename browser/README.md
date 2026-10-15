# Browser

`browser/` contains Doe's browser-facing surfaces:

- `browser/workspace/`
  - Fawn's durable browser workspaces: the shared observation, action, and
    recovery service above stock Chromium, behavior-qualified on its frozen case
    set and advantaged only on its frozen differentiation suite (see its
    [README](workspace/README.md)). The
    [workspace contract](../docs/fawn-workspace-contract.md) owns its
    requirements and [status](../docs/status/fawn-workspace.md) records what is
    established.
- `browser/chromium/`
  - optional Chromium runtime-integration contracts, scripts, and diagnostics,
    plus the hosted demonstration pages under `resources/`
- `browser/chromium_webgpu_lane/`
  - the Chromium checkout/build tree when kept in-tree

Chromium hooks and browser-runtime integration are added only when measured benefits justify their compatibility
and maintenance costs; Dawn remains the incumbent browser runtime.

Keep workspace authority, browser smoke, browser benchmark projection, and
Chromium checkout management separate even when they share scripts or artifacts.
