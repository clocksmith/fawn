# WGSL-language control supplement

This post-handoff fixture realizes the presealed P06 requirement to keep WGSL
language exports equal to the default control. It was not part of the blind
executable fixture seal. It calls actual enumeration, known/unknown membership
and free exports with a real instance, directly and using both proc-name length
forms. Independent source-built Z observations are the comparison control for
F, FC and FCI; author-prebuilt controls remain separately labelled. No changed
WGSL expectation is learned from selected candidate output and no GPU is used.
