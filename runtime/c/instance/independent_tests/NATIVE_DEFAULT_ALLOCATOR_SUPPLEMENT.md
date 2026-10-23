# Default allocator regression supplement

Presealed N05 requires adjacent default allocator obligations as well as the
future/callback/loss/lease/pipeline/work-done/trace cases. Review of the selected
23-name inventory found it omitted the retained command-storage allocator
identity test. The original 23-name inventory and root remain untouched. A v2
root and filter list add that one unchanged test, for 24 expected matches. This
is an explicitly post-handoff coverage correction, not blind test authorship.
No new runtime behavior or GPU is introduced, and no assertion is relaxed.
