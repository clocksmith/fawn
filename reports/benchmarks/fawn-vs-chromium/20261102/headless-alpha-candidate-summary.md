Presentation mode: headless. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- heavy_particles: GPU-process graphics memory p50 (MB) 0.96x [0.95, 0.98]

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: frame/op completion p50 p50 (ms) 1.28x [1.12, 1.33]
- heavy_particles: GPU-process CPU per frame/op p50 (ms) 1.24x [1.06, 1.32]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.30x [1.13, 1.41]

## Per page

### heavy_particles

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 67.1 [62.2, 109] | 77.8 [72.2, 79.8] | 73.0 [71.1, 78.8] | 1.16 [0.71, 1.28] | no clear difference | 1.09 [0.66, 1.26] |
| cold first job (nav -> first completed job) p95 (ms) | 125 [69.6, 125] | 128 [79.6, 128] | 80.4 [73.6, 80.4] | 1.03 [0.64, 1.84] | no clear difference | 0.64 [0.59, 1.16] |
| launch -> first completed job p50 (ms) | 693 [599, 994] | 742 [718, 743] | 653 [642, 719] | 1.07 [0.75, 1.24] | no clear difference | 0.94 [0.65, 1.09] |
| frame/op completion p50 p50 (ms) | 3.60 [3.50, 4.10] | 4.60 [4.60, 4.70] | 3.60 [3.60, 3.70] | 1.28 [1.12, 1.33] | loss | 1.00 [0.88, 1.03] |
| frame/op completion p95 p50 (ms) | 4.60 [4.50, 6.30] | 6.20 [5.40, 7.20] | 4.80 [4.20, 4.80] | 1.35 [0.92, 1.60] | no clear difference | 1.04 [0.71, 1.07] |
| frame/op completion p99 p50 (ms) | 5.00 [4.70, 6.70] | 7.30 [6.10, 7.90] | 5.20 [4.80, 5.80] | 1.46 [0.96, 1.61] | no clear difference | 1.04 [0.72, 1.18] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.5, 16.7] | 16.7 [16.6, 16.7] | 16.6 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 0.99 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.2 [18.1, 19.3] | 18.8 [18.7, 19.6] | 18.4 [18.3, 18.8] | 1.03 [0.97, 1.08] | no clear difference | 1.01 [0.95, 1.03] |
| completed-frame interval p99 p50 (ms) | 19.0 [18.5, 19.9] | 19.8 [19.5, 20.3] | 19.5 [19.2, 19.5] | 1.04 [0.99, 1.08] | no clear difference | 1.03 [0.97, 1.05] |
| GPU-process CPU per frame/op p50 (ms) | 1.78 [1.73, 1.95] | 2.20 [2.05, 2.31] | 1.92 [1.69, 2.22] | 1.24 [1.06, 1.32] | loss | 1.08 [0.95, 1.27] |
| all-process CPU per frame/op p50 (ms) | 2.75 [2.68, 3.01] | 3.58 [3.38, 3.77] | 3.20 [2.94, 3.73] | 1.30 [1.13, 1.41] | loss | 1.16 [1.02, 1.39] |
| GPU-process memory p50 (MB) | 202 [201, 205] | 197 [196, 200] | 202 [200, 203] | 0.97 [0.96, 0.99] | no clear difference | 1.00 [0.98, 1.01] |
| GPU-process graphics memory p50 (MB) | 151 [151, 152] | 144 [144, 148] | 151 [148, 152] | 0.96 [0.95, 0.98] | win | 1.00 [0.98, 1.01] |
| total browser memory p50 (MB) | 363 [362, 365] | 359 [358, 362] | 365 [365, 367] | 0.99 [0.98, 1.00] | no clear difference | 1.01 [1.00, 1.01] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 77.8 | 73.0 | 1.07 [0.99, 1.12] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 128 | 80.4 | 1.60 [0.99, 1.74] | no clear difference |
| launch -> first completed job p50 (ms) | 742 | 653 | 1.14 [1.03, 1.16] | loss |
| frame/op completion p50 p50 (ms) | 4.60 | 3.60 | 1.28 [1.24, 1.31] | loss |
| frame/op completion p95 p50 (ms) | 6.20 | 4.80 | 1.29 [1.12, 1.62] | loss |
| frame/op completion p99 p50 (ms) | 7.30 | 5.20 | 1.40 [1.10, 1.65] | loss |
| completed-frame interval p50 p50 (ms) | 16.7 | 16.6 | 1.01 [0.99, 1.01] | no clear difference |
| completed-frame interval p95 p50 (ms) | 18.8 | 18.4 | 1.02 [0.99, 1.07] | no clear difference |
| completed-frame interval p99 p50 (ms) | 19.8 | 19.5 | 1.02 [1.00, 1.05] | no clear difference |
| GPU-process CPU per frame/op p50 (ms) | 2.20 | 1.92 | 1.15 [0.94, 1.30] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 3.58 | 3.20 | 1.12 [0.91, 1.24] | no clear difference |
| GPU-process memory p50 (MB) | 197 | 202 | 0.98 [0.96, 0.99] | no clear difference |
| GPU-process graphics memory p50 (MB) | 144 | 151 | 0.96 [0.95, 0.99] | win |
| total browser memory p50 (MB) | 359 | 365 | 0.98 [0.98, 0.99] | no clear difference |
