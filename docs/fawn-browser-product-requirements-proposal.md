# Product Requirements Document: Fawn, a Browser with Shared Workspaces for People and Agents

**Status: Proposed, not validated.** This document defines proposed requirements, not established capabilities or release readiness. It authorizes no contact, purchase, pilot, production use, sales activity, or consumer exposure. Performance, adoption, and supervision benefits remain hypotheses. No price or valuation is proposed.

## 1. Product thesis, differentiation, and users

**Fawn is a browser built around DoeRuntime as its custom WebGPU implementation, with persistent shared workspaces for people and agents.** People browse, use web applications, inspect artifacts, and delegate bounded work within the same environment.

DoeRuntime owns supported WGSL compilation and GPU execution through Vulkan, Metal, and D3D12. Fawn owns the browser experience, workspace coordination, and user-facing authority. These are related components of the same project, with separate responsibilities and acceptance requirements.

Ordinary browsing must work without creating a job, choosing an agent, or approving a plan. Jobs organize delegated work; they do not become a prerequisite for navigation or web-application use.

**Why switch?** Hypothesis: Fawn combines useful browsing, persistent collaboration, and dependable GPU execution with less intervention and repeated setup than the user's existing arrangement. Any latency, memory, privacy, or recovery advantage must be measured separately.

**How does evaluation begin?** Select a complete browser-centered workflow with identifiable inputs, acceptance criteria, and an appropriate comparison. A useful evaluation should exercise the proposed browser capabilities rather than merely demonstrate that an agent can complete a portal form.

**What accumulates?** Hypothesis: maintained browser integrations, compiler/runtime corrections, and workspace-specific knowledge tied to sources and versions improve subsequent work. Sensitive workspace history is not automatically pooled across users or organizations.

**What must a competitor reproduce?** Comparable user outcomes, compatibility, reliability, integration quality, and improvement velocity. A competitor need not copy Doe or Fawn's internal architecture. Owning a custom runtime does not establish differentiation by itself.

Users include people browsing independently, workspace owners, contributors, reviewers, and authorized agents. Engineering investigation, document analysis, creative applications, simulations, and portal operations are candidate evaluations. Freight receivables is one possible example, never the product definition.

## 2. Critical user journeys and scope

### Primary journey: return to a browser workspace and continue useful work

A person opens a workspace containing source pages, documents, and a GPU-accelerated web application. They browse normally and authorize an agent to prepare a bounded analysis from named input versions. The person continues other browsing while the agent works.

The application uses Fawn's selected WebGPU implementation. The workspace associates resulting artifacts with their inputs and execution evidence. A contributor corrects an input; the earlier artifact retains its original meaning, while conclusions dependent on the changed input require rechecking.

An interruption closes the browser or stops a worker. On return, Fawn restores the workspace's durable records and reconstructible browser state. It identifies completed artifacts, interrupted assignments, unavailable sessions, and required decisions.

**Restoration does not imply arbitrary GPU-state persistence.** Applications recreate device resources and recover from supported checkpoints or rerun unfinished computation. Fawn does not describe partially executed GPU work as completed or assume that credentials remain valid.

The person inspects the result and accepts or rejects it against the stated criteria. External publication or submission, when required, is a separate authorized action.

Hypothesis: this reduces the effort of rediscovering context, coordinating participants, and resuming work while preserving ordinary browser usefulness.

### Consequential-action journey: reconcile before repeating

An authorized worker submits a prepared artifact. Fawn records the approved content, destination and account, version, limits, and attempt. The connection fails before confirmation reaches the workspace.

The outcome remains unknown. Fawn checks the destination using the connector's declared reconciliation method. Evidence may establish acceptance, rejection, or continued uncertainty. Absence from a search result is not automatically proof that the action never happened.

Fawn never automatically retries an unresolved consequential action. Reliable destination-supported duplicate prevention or authoritative reconciliation may permit a safe retry. Otherwise, the action remains blocked. A human-authorized retry under uncertainty records the risk and the new authorization; fresh approval alone does not guarantee absence of duplication.

A job is accepted only when every required acceptance condition is satisfied. Unresolved required submissions prevent acceptance. Closing or abandoning a job is distinct from accepting it.

### Other journeys

People browse without delegation, collaborate on versioned artifacts, inspect resource use, change or revoke grants, hand assignments to another worker, and return to earlier work. Supported artifact sharing does not implicitly share credentials, authenticated sessions, or every open tab.

The first release supports a limited set of complete journeys. External-action jobs require defined approval and reconciliation methods. Reading, local analysis, and simulation need not have an external destination.

## 3. Interaction and state model

**The primary interface is the browser:** navigation, tabs, page content, profiles, and workspace switching. Workspace controls expose participants, artifacts, running assignments, resource use, and pending decisions beside that experience.

Assignment boards, action history, evidence inspection, and detailed approvals are contextual views. They must not turn ordinary browsing into a sequence of administrative forms.

Chat supplies instructions and clarification. Important approvals, acceptance decisions, artifacts, and external observations also exist as structured objects. A message alone cannot silently create execution authority.

A workspace contains resources, versioned artifacts, participants, and optional jobs. Jobs contain bounded assignments tied to specific inputs. Work states include queued, running, blocked, paused, awaiting approval, awaiting review, completed, and stopped.

External action outcomes are separate: never dispatched, confirmed successful, confirmed failed, or unresolved. Submission, receipt, and acceptance are distinguished according to the destination's actual evidence.

Historical observations remain immutable. A changed document may invalidate a current conclusion or authorization without erasing the fact that an earlier version was accepted.

Every blocked item identifies what is missing and who can resolve it. Stopping prevents new actions while preserving outstanding attempts and explaining effects that may already have occurred.

## 4. Architecture and dependencies

### Browser and GPU integration

Fawn integrates DoeRuntime at the browser's actual WebGPU implementation boundary. A JavaScript wrapper delegating to an incumbent implementation does not establish this integration.

The integration must preserve required browser isolation, validation, resource ownership, and failure propagation. Backends and operating-system configurations qualify independently. Replacing WebGPU does not imply replacing page rendering, compositing, video decoding, or all other graphics subsystems.

An incumbent provider remains an explicit development comparator and, where implemented, a clearly identified rollback option. No silent fallback may masquerade as Doe execution. Intermediate workspace builds may use the incumbent, but do not establish completion of the integrated target.

**Doe integration is an architectural requirement; an advantage over Dawn remains unproved.** Fawn's workspace implementation can progress independently of a demonstrated speedup. Platform APIs and drivers remain dependencies.

### Component responsibilities

| Component                                   | Responsibility                                                                                                        |
| ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Browser shell                               | Navigation, tabs, profiles, downloads, workspace presentation, and user controls.                                     |
| Browser permission and integration boundary | Connect tools and browser processes to authorized actions while preserving origin, profile, and workspace separation. |
| Workspace store                             | Durable jobs, artifact versions, dependencies, participants, observations, and decisions.                             |
| Scheduler                                   | Bounded assignments, resource limits, worker ownership, and reassignment.                                             |
| Action executor                             | Recheck current authority before dispatch and retain uncertain external outcomes.                                     |
| Destination connectors                      | Supported structured or interface actions and destination-specific reconciliation.                                    |
| DoeRuntime                                  | Shader compilation, GPU resources, recording, submission, completion, and backend diagnostics.                        |

Workers propose actions; they cannot bypass the action executor by asserting that approval exists. A resumed obsolete worker must not regain dispatch authority after reassignment.

Prefer reliable APIs for agent actions when appropriate. This reduces unnecessary interface automation; it does not make human browsing a fallback.

Models and tools remain replaceable. Verification uses independent evidence and can reject an output. Journals do not make remote services transactional. GPU completion tracking does not establish external submission acceptance.

## 5. Privacy, safety, and accessibility

Access is scoped by workspace, resource, action, and duration. Viewing is not submitting; one approved version cannot authorize changed content, destinations, accounts, amounts, or limits.

Secrets remain outside model-visible artifacts and ordinary logs. A shared artifact does not grant control of its source session. External pages and documents supply task data, not authority to widen permissions or budgets.

Revocation prevents new actions. Already dispatched actions retain explicit outcomes or uncertainty. Browser-origin permissions, workspace membership, agent permissions, and GPU-resource ownership remain distinct controls.

Retention, deletion, and export describe local and connected-provider boundaries. Deleting a workspace does not undo an external effect. Failed workers cannot broaden private-material visibility to continue a job.

Keyboard navigation, screen-reader labels, understandable statuses, non-color warnings, and asynchronous review are requirements throughout the browser and workspace interface.

## 6. Economics and operations

For delegated jobs, count cost per accepted outcome, including failures, abandonment, rework, and human supervision. Separately measure browser resource use, startup, responsiveness, and operational maintenance. The browser is not evaluated solely by completed-job volume.

Show known budgets and uncertainty before authorized work. Track model/tool charges, browser and compute consumption, storage, external costs, intervention, and integration maintenance. Enforce limits before starting additional work and release idle resources only when ownership permits.

Pricing remains open. Free distribution and open-source adoption are compatible with the product. **Early product evidence is repeated independent use of valuable workflows; payment is not a prerequisite for technical or product validation.**

## 7. Validation, rollout, and open decisions

Evaluate three responsibilities separately before interpreting combined results:

- **Browser:** web compatibility, navigation, responsiveness, profiles, installation, update behavior, isolation, and crash recovery.
- **Workspace:** versioning, handoff, authority, stale conclusions, reconciliation, human effort, and accepted outcomes.
- **Doe integration:** actual provider identity, independent output checks, lifecycle behavior, CPU cost, memory, and latency on declared hardware.

Run the combined primary journey afterward. Match models, inputs, permissions, tool access, budgets, and acceptance criteria where appropriate. Compare against competent alternatives, including their existing persistence and recovery mechanisms.

Separate deterministic recovery tests from model-driven completion. Include changed inputs, revoked grants, budget exhaustion, obsolete workers, browser or GPU-process interruption, and crashes immediately before and after external effects. Freeze thresholds before evaluation; failures and abandonment remain in denominators.

**Source-reported baseline, not independently verified for this revision:** the supplied draft reports no material advantage in the tested Playwright comparison, unmet changing-content and human-handoff cases against a stronger control, an unexecuted model-driven comparison, and no external adopters. It establishes neither integrated-browser readiness nor Doe superiority over Dawn.

Progress through internal complete journeys, independently authorized users, and repeated use under realistic interruption. This proposal does not itself authorize any rollout.

Open decisions include the initial browser platform and hardware configuration, supported recovery state, collaboration boundaries, first representative workflow, integration set, concurrency requirements, and reliably reconcilable external actions.

**Release question:** Can people use Fawn as a dependable browser, benefit from its integrated GPU execution and shared workspaces, and delegate useful work with less intervention without losing compatibility, privacy, or control?

---

## Revision provenance

Proposed revision of the supplied `Pasted markdown(20261025-114706).md`. This version makes ordinary browsing primary, makes Doe integration an architectural requirement rather than optional workload acceleration, replaces the early paid-use gate with independent use, and narrows acceptance and duplicate-prevention guarantees. All additions are proposed requirements. No repository inspection, implementation, test execution, hardware qualification, or external action was performed for this revision.