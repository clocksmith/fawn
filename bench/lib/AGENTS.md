# Shared benchmark helpers

Inherit [benchmark instructions](../AGENTS.md). Keep helpers focused and importable
without running commands. Native/package legacy cube consumers still use this
directory; registered inventory rules belong to [inventory](../inventory/AGENTS.md).

`receipt_provenance.py` records observed host/artifact identities for new receipts;
it cannot infer a binary's source from the checkout HEAD or grant qualification.
Compatibility imports delegate to the canonical owner rather than duplicating it.
Run tests for the changed consumer and helper.
