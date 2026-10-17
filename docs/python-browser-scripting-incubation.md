# Python-native browser scripting

**Status: incubating stretch goal; parked, documentation only.** No runtime,
browser fork, dependency installation, or implementation experiment is authorized.

Question: could first-class Python scripting inside a browser make a specific
developer workflow materially easier, faster, or more capable than existing
alternatives?

## Priority and compatibility boundaries

[Doe's goals](../GOALS.md) and the [Chromium integration plan](../browser/chromium/acceptance.md)
remain authoritative: unchanged-application improvements over Dawn first, actual
page WebGPU ownership next, and separately qualified wider GPU paths thereafter;
maintainability is measured throughout. Vulkan remains the focus, Metal the
confirmation path, and D3D12 deferred. This proposal changes none of those priorities.

Python would be an additional, explicitly selected browser capability. Existing
websites retain JavaScript compatibility and V8; Python must not replace V8 by
default. Browser scripting is outside the WGSL compiler's responsibility and
does not justify rewriting the browser.

## Established precedents

- [Grail](https://grail.sourceforge.net/) was a real Python browser supporting
  [downloaded Python applets](https://grail.sourceforge.net/info/summary.html).
  Its historical restricted execution mechanism is not a modern sandbox design.
- Mozilla's [PyDOM/language-neutral DOM work](https://wiki.mozilla.org/Breaking_the_grip_JS_has_on_the_DOM)
  landed ([bug 255942, comment 23](https://bugzilla.mozilla.org/show_bug.cgi?id=255942#c23));
  the abstraction was later removed
  ([comment 34](https://bugzilla.mozilla.org/show_bug.cgi?id=255942#c34),
  [removal bug](https://bugzilla.mozilla.org/show_bug.cgi?id=570738)).
  Ordinary Firefox did not ship bundled CPython; optional Python integration
  and a landed language-neutral interface are different from default availability.
- [Python-WebKit](https://www.gnu.org/software/pythonwebkit/) exposed WebKit's
  DOM and events to native Python, primarily for embedded desktop applications.
- [Pyodide](https://pyodide.org/en/stable/) runs CPython compiled to WebAssembly
  in existing browsers, with JavaScript interoperability and supported packages.
  [PyScript](https://github.com/pyscript/pyscript) builds Python browser applications
  using runtimes including Pyodide and MicroPython.
  [Brython](https://www.brython.info/) instead
  [translates Python into JavaScript](https://brython.info/gallery/turtle.html).
  These are distinct approaches, not interchangeable names for native CPython.
- [Dartium's technology preview](https://blog.chromium.org/2012/02/tech-preview-of-chromium-with-dart.html)
  added a Dart VM alongside JavaScript rather than replacing JavaScript.

These precedents establish technical availability, not developer satisfaction
or an unmet need for another runtime.

## Intended workflow and unverified hypothesis

A developer has a pure-Python CSV validation function and wants to share an
interactive browser form with colleagues: paste rows, validate them, inspect
errors, cancel, and rerun without translating the function into JavaScript.
Pyodide can already support this workflow. Its runtime/package delivery and
browser bindings require setup; an external controller requires a Python
installation and coordination with the page. Whether either burden is material
for these developers is unverified.

The hypothesis is that integrated Python could reduce setup and debugging steps
while preserving responsive interaction. No unique capability, speed advantage,
package compatibility advantage, or user preference is established.

| Approach | Existing capability or proposed mechanism | Comparison obligation |
| --- | --- | --- |
| Python through WebAssembly | Pyodide in stock browsers, optionally with PyScript's application layer | Measure delivery, startup, supported packages, bindings, and worker/cancellation behavior before assuming native integration helps. |
| Sandboxed native Python with browser bindings | Proposed interpreter integration with mediated DOM access | Establish isolation and lifecycle semantics; account for browser installation, bindings, crash containment, and rebasing. Native CPython alone supplies no sandbox. |
| External Python controller | Supported automation such as [Playwright Python](https://playwright.dev/python/docs/intro); Python executes outside the page | Measure installation and communication costs against reuse of existing Python packages and browser automation. |

[Fawn already provides](../browser/workspace/README.md) durable browser workspaces
above stock Chromium through a JavaScript service with Playwright and private-pipe
CDP adapters. Its dispatcher supports navigation, click, fill, select, submit,
and download with authority and recovery checks. That is existing automation
infrastructure, not a shipped Python SDK or an in-page Python runtime. Any future
Python client for Fawn must use its service authority; a client bridge remains
proposed. Attaching another controller directly to its private browser would
bypass that boundary. A standalone Playwright Python baseline can use its own
browser without adding a runtime to Fawn.

## Proposed ownership and unresolved requirements

Browser integration would own origins, permissions, DOM access, and lifecycle.
Python would own language execution. Doe would own GPU execution only at an
explicitly integrated and qualified boundary. Python support does not
automatically execute Python code on the GPU.

Native CPython is not a sandbox. Untrusted pages must not inherit unrestricted
filesystem, process, network, or native-extension access. Before any promotion,
resolve process isolation, permission mediation, cross-origin and iframe
boundaries, resource limits, cancellation, and navigation cleanup. Define how
capabilities and callbacks are revoked, how stale results are rejected, and how
an interpreter crash is contained. A trusted local controller must likewise
not expose its host privileges to page content.

Practical costs remain open: interpreter download/install footprint, startup,
memory, package and native-extension compatibility, browser event-loop
integration, Python/JavaScript object lifetime and cross-language garbage
collection, debugging, crash containment, and Chromium rebase maintenance.
Record these costs even when the demonstration works.

## Future experiment — not authorized implementation

If separately authorized, compare the exact CSV workflow using a minimal page
with an input, Run button, Python event handler, result area, and Cancel action.
Use bounded asynchronous validation with explicit input/work limits; navigation
must cancel work, release listeners and objects, and prevent late DOM updates.
Keep input, algorithm, output, and failure cases identical across the proposed
native runtime, Pyodide, and an external Python controller. In the external case,
the event reaches a controller-side Python handler through an explicit bridge.

Record installation steps and effort, downloaded/installed bytes, cold and warm
startup, interaction latency, peak/retained memory, cancellation, navigation
cleanup, and recovery from errors or interpreter/controller failure. Pin browser,
runtime, packages, hardware, cache state, repetitions, and permissions. Measure
developer task completion, mistakes, and satisfaction separately from runtime
measurements. Add a GPU case only to test a separately meaningful benefit.

Promotion requires a reproducible workflow advantage that survives security and
maintenance costs, with a separately approved owner and scope. A working Python
button is insufficient. Keep this proposal parked if existing approaches deliver
the same experience more simply.
