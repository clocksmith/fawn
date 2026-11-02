Presentation mode: headed. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- none

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- compute_atomic_canvas: cold first job (nav -> first completed job) p95 (ms) 1.44x [1.06, 1.64]

## Per page

### compute_atomic_canvas

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , oracle 6/6, pixel hashes 1
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , oracle 6/6, pixel hashes 1
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , oracle 6/6, pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 110 [98.3, 112] | 118 [98.6, 135] | 119 [105, 152] | 1.08 [0.90, 1.35] | no clear difference | 1.08 [0.95, 1.42] |
| cold first job (nav -> first completed job) p95 (ms) | 126 [110, 126] | 181 [133, 181] | 180 [128, 180] | 1.44 [1.06, 1.64] | loss | 1.43 [1.02, 1.63] |
| launch -> first completed job p50 (ms) | 758 [719, 832] | 788 [773, 835] | 783 [744, 879] | 1.04 [0.94, 1.15] | no clear difference | 1.03 [0.90, 1.21] |
| frame/op completion p50 p50 (ms) | 33.1 [32.9, 33.2] | 33.3 [33.1, 33.5] | 33.2 [32.9, 33.3] | 1.01 [1.00, 1.02] | no clear difference | 1.00 [0.99, 1.01] |
| frame/op completion p95 p50 (ms) | 34.2 [33.6, 34.8] | 35.6 [33.7, 36.0] | 33.7 [33.6, 35.0] | 1.04 [0.98, 1.07] | no clear difference | 0.99 [0.97, 1.03] |
| frame/op completion p99 p50 (ms) | 55.5 [43.5, 56.7] | 45.0 [43.9, 55.7] | 56.9 [43.7, 57.6] | 0.81 [0.78, 1.03] | no clear difference | 1.03 [0.78, 1.31] |
| completed-frame interval p50 p50 (ms) | 33.5 [33.4, 34.6] | 36.2 [33.5, 47.6] | 33.6 [33.4, 49.3] | 1.08 [0.98, 1.43] | no clear difference | 1.00 [0.97, 1.48] |
| completed-frame interval p95 p50 (ms) | 50.5 [50.2, 51.4] | 50.7 [50.1, 51.0] | 50.5 [49.0, 50.6] | 1.00 [0.98, 1.01] | no clear difference | 1.00 [0.97, 1.01] |
| completed-frame interval p99 p50 (ms) | 51.1 [50.6, 51.6] | 52.0 [50.7, 52.6] | 50.9 [50.3, 51.8] | 1.02 [0.99, 1.03] | no clear difference | 1.00 [0.98, 1.02] |
| GPU-process CPU per frame/op p50 (ms) | 1.76 [1.65, 2.16] | 1.97 [1.81, 2.23] | 2.14 [2.05, 2.59] | 1.12 [0.91, 1.31] | no clear difference | 1.22 [0.95, 1.52] |
| all-process CPU per frame/op p50 (ms) | 3.06 [2.85, 3.69] | 3.71 [3.42, 4.19] | 3.93 [3.53, 4.43] | 1.21 [0.99, 1.43] | no clear difference | 1.29 [1.02, 1.48] |
| GPU-process memory p50 (MB) | 155 [150, 157] | 156 [144, 157] | 157 [147, 157] | 1.01 [0.92, 1.04] | no clear difference | 1.01 [0.94, 1.04] |
| GPU-process graphics memory p50 (MB) | 127 [119, 129] | 127 [116, 128] | 129 [119, 129] | 1.00 [0.90, 1.07] | no clear difference | 1.01 [0.92, 1.08] |
| total browser memory p50 (MB) | 321 [302, 326] | 326 [305, 332] | 326 [319, 329] | 1.01 [0.95, 1.08] | no clear difference | 1.01 [0.99, 1.08] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 118 | 119 | 1.00 [0.69, 1.27] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 181 | 180 | 1.01 [0.74, 1.41] | no clear difference |
| launch -> first completed job p50 (ms) | 788 | 783 | 1.01 [0.89, 1.11] | no clear difference |
| frame/op completion p50 p50 (ms) | 33.3 | 33.2 | 1.00 [1.00, 1.02] | no clear difference |
| frame/op completion p95 p50 (ms) | 35.6 | 33.7 | 1.06 [0.99, 1.07] | no clear difference |
| frame/op completion p99 p50 (ms) | 45.0 | 56.9 | 0.79 [0.77, 1.03] | no clear difference |
| completed-frame interval p50 p50 (ms) | 36.2 | 33.6 | 1.08 [0.69, 1.42] | no clear difference |
| completed-frame interval p95 p50 (ms) | 50.7 | 50.5 | 1.00 [0.99, 1.03] | no clear difference |
| completed-frame interval p99 p50 (ms) | 52.0 | 50.9 | 1.02 [0.99, 1.04] | no clear difference |
| GPU-process CPU per frame/op p50 (ms) | 1.97 | 2.14 | 0.92 [0.76, 1.09] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 3.71 | 3.93 | 0.94 [0.83, 1.13] | no clear difference |
| GPU-process memory p50 (MB) | 156 | 157 | 0.99 [0.92, 1.07] | no clear difference |
| GPU-process graphics memory p50 (MB) | 127 | 129 | 0.99 [0.90, 1.07] | no clear difference |
| total browser memory p50 (MB) | 326 | 326 | 1.00 [0.94, 1.03] | no clear difference |
