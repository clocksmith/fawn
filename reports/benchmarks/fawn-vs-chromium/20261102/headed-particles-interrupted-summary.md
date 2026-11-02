Presentation mode: headed. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- none

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: frame/op completion p95 p50 (ms) 1.77x [1.51, 1.84]
- heavy_particles: frame/op completion p99 p50 (ms) 1.76x [1.53, 1.85]
- heavy_particles: completed-frame interval p95 p50 (ms) 1.09x [1.06, 1.13]
- heavy_particles: completed-frame interval p99 p50 (ms) 1.08x [1.06, 1.12]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.66x [1.05, 1.91]

## Per page

### heavy_particles

- chrome: 3/3 ok, Doe mapped {'False': 3}, adapter apple / metal-3 /  / , pixel hashes 3
- fawn_dawn: 3/3 ok, Doe mapped {'False': 3}, adapter apple / metal-3 /  / , pixel hashes 3
- fawn_doe: 3/3 ok, Doe mapped {'True': 3}, adapter apple / metal-3 /  / , pixel hashes 3
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 108 [71.1, 132] | 94.4 [81.8, 286] | 81.1 [73.8, 235] | 0.87 [0.62, 4.03] | no clear difference | 0.75 [0.56, 3.31] |
| cold first job (nav -> first completed job) p95 (ms) | 132 [71.1, 132] | 286 [81.8, 286] | 235 [73.8, 235] | 2.17 [0.62, 2.65] | no clear difference | 1.78 [0.56, 2.18] |
| launch -> first completed job p50 (ms) | 1258 [659, 1319] | 975 [803, 1466] | 716 [711, 957] | 0.78 [0.61, 2.22] | no clear difference | 0.57 [0.54, 1.45] |
| frame/op completion p50 p50 (ms) | 2.90 [2.90, 3.10] | 3.20 [3.10, 3.30] | 3.00 [2.90, 3.20] | 1.10 [1.00, 1.14] | no clear difference | 1.03 [0.94, 1.10] |
| frame/op completion p95 p50 (ms) | 3.10 [3.10, 3.50] | 5.50 [5.30, 5.70] | 3.30 [3.20, 3.60] | 1.77 [1.51, 1.84] | loss | 1.06 [0.91, 1.16] |
| frame/op completion p99 p50 (ms) | 3.30 [3.30, 3.60] | 5.80 [5.50, 6.10] | 3.50 [3.30, 3.80] | 1.76 [1.53, 1.85] | loss | 1.06 [0.92, 1.15] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.7, 16.7] | 16.7 [16.7, 16.7] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [1.00, 1.01] |
| completed-frame interval p95 p50 (ms) | 17.4 [17.2, 17.7] | 19.0 [18.8, 19.4] | 17.4 [17.4, 17.4] | 1.09 [1.06, 1.13] | loss | 1.00 [0.98, 1.01] |
| completed-frame interval p99 p50 (ms) | 17.8 [17.7, 18.0] | 19.2 [19.1, 19.8] | 17.8 [17.8, 17.9] | 1.08 [1.06, 1.12] | loss | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 0.74 [0.74, 1.08] | 1.20 [1.06, 1.32] | 0.95 [0.89, 1.31] | 1.61 [0.98, 1.77] | no clear difference | 1.27 [0.83, 1.77] |
| all-process CPU per frame/op p50 (ms) | 1.18 [1.17, 1.68] | 1.96 [1.77, 2.24] | 1.68 [1.57, 2.25] | 1.66 [1.05, 1.91] | loss | 1.42 [0.93, 1.91] |
| GPU-process memory p50 (MB) | 202 [198, 205] | 201 [195, 204] | 205 [202, 209] | 0.99 [0.95, 1.03] | no clear difference | 1.01 [0.99, 1.05] |
| GPU-process graphics memory p50 (MB) | 152 [151, 155] | 149 [146, 152] | 154 [151, 158] | 0.98 [0.95, 1.01] | no clear difference | 1.01 [0.98, 1.04] |
| total browser memory p50 (MB) | 391 [380, 391] | 389 [378, 393] | 392 [387, 402] | 1.00 [0.97, 1.03] | no clear difference | 1.00 [0.99, 1.06] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 94.4 | 81.1 | 1.16 [0.35, 3.88] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 286 | 235 | 1.22 [0.35, 3.88] | no clear difference |
| launch -> first completed job p50 (ms) | 975 | 716 | 1.36 [0.84, 2.06] | no clear difference |
| frame/op completion p50 p50 (ms) | 3.20 | 3.00 | 1.07 [0.97, 1.14] | no clear difference |
| frame/op completion p95 p50 (ms) | 5.50 | 3.30 | 1.67 [1.47, 1.78] | loss |
| frame/op completion p99 p50 (ms) | 5.80 | 3.50 | 1.66 [1.45, 1.85] | loss |
| completed-frame interval p50 p50 (ms) | 16.7 | 16.7 | 1.00 [1.00, 1.00] | no clear difference |
| completed-frame interval p95 p50 (ms) | 19.0 | 17.4 | 1.09 [1.08, 1.11] | loss |
| completed-frame interval p99 p50 (ms) | 19.2 | 17.8 | 1.08 [1.07, 1.11] | loss |
| GPU-process CPU per frame/op p50 (ms) | 1.20 | 0.95 | 1.26 [0.81, 1.47] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 1.96 | 1.68 | 1.16 [0.79, 1.42] | no clear difference |
| GPU-process memory p50 (MB) | 201 | 205 | 0.98 [0.93, 1.01] | no clear difference |
| GPU-process graphics memory p50 (MB) | 149 | 154 | 0.97 [0.93, 1.01] | no clear difference |
| total browser memory p50 (MB) | 389 | 392 | 0.99 [0.94, 1.01] | no clear difference |
