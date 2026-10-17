# Status policy

Live status files contain only:

- the current promoted boundary;
- active blockers;
- source-of-truth artifact pointers;
- one history pointer.

They do not contain dated chronology, copied benchmark results, hashes, pass
counts, or closed implementation notes. Remove resolved narrative from the live
boundary; Git history retains it. Link an exact commit when a historical record
is needed, preserving its meaning without copying it into a tracked archive.

Artifacts remain authoritative under [evidence retention](../../reports/README.md).
Do not delete an active gate, fixture, release or claim dependency as history.
Historical instructions and next actions never select current work.
