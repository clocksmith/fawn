# Resource ownership

Follow the common C guide and parent charter. This owner adopts real native
resources; never create a synthetic backend or publish borrowed native storage
as owned. Preserve same-device identity, transactional acquisition, first-error
recording and unknown-submission obligations. Keep map/future delivery, labels
and diagnostics in their existing owners. Normalize named format semantics at
native ABI adapters: retained Zig/header numeric identities currently differ.
The focused check is `make -C runtime/c/resources check`; outputs belong in `/tmp`.
Render attachment begin/end packets must bracket every query event, including
empty scopes. Never infer pass closure from the last draw. Map completion pointers
start at the admitted span; const/read admission must not become writable storage.
