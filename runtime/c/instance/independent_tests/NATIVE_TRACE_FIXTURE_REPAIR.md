# Trace runner setup repair

First author-prebuilt Z public C consumer passed, but the supplementary trace
checker found no output file. This is runner setup, not an owner failure: pinned
appendLocked uses openFile(write_only), never createFile. The caller must create
the trace destination first. The original successful consumer receipt and missing
trace outcome are retained; new explicitly tracefix-labelled runs precreate fresh
empty destinations. No source or acceptance assertion was changed.
