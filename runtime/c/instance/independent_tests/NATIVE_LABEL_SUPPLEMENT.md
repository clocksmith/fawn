# Label regression supplement

Written after fixed native source handoff as a concrete realization of sealed
N05 label/allocator regressions. Sixty-four real native instance lifetimes per
provider copy a seven-byte embedded-NUL label, overwrite its source, retain and
release, and check the copied bytes. Odd iterations transfer the last instance
lease to an actual unconfigured surface. Label survives nonfinal releases and
vanishes after final release. Final observation uses only the label-store integer
key; no freed instance memory is inspected and no spare owner reference is kept.
This supplements, rather than retroactively changes, preimplementation fixtures.
