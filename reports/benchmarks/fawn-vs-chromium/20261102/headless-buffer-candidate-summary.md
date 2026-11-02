Presentation mode: headless. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- heavy_particles: GPU-process memory p50 (MB) 0.96x [0.96, 0.99]
- heavy_particles: GPU-process graphics memory p50 (MB) 0.94x [0.94, 0.98]

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 1.35x [1.18, 1.41]
- heavy_particles: frame/op completion p50 p50 (ms) 1.08x [1.03, 1.17]
- heavy_particles: frame/op completion p95 p50 (ms) 1.17x [1.03, 1.47]
- heavy_particles: frame/op completion p99 p50 (ms) 1.15x [1.09, 1.36]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.22x [1.08, 1.40]

## Per page

### heavy_particles

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 56.1 [54.4, 64.1] | 75.6 [73.1, 78.7] | 71.9 [70.6, 76.3] | 1.35 [1.18, 1.41] | loss | 1.28 [1.11, 1.38] |
| cold first job (nav -> first completed job) p95 (ms) | 107 [58.2, 107] | 117 [76.5, 117] | 85.6 [75.2, 85.6] | 1.10 [0.72, 2.01] | no clear difference | 0.80 [0.71, 1.47] |
| launch -> first completed job p50 (ms) | 582 [573, 833] | 637 [593, 722] | 718 [616, 731] | 1.10 [0.76, 1.25] | no clear difference | 1.23 [0.83, 1.27] |
| frame/op completion p50 p50 (ms) | 3.60 [3.50, 3.70] | 3.90 [3.80, 4.10] | 3.70 [3.60, 4.20] | 1.08 [1.03, 1.17] | loss | 1.03 [0.97, 1.20] |
| frame/op completion p95 p50 (ms) | 5.90 [4.70, 6.10] | 6.90 [6.10, 7.10] | 6.20 [4.80, 6.30] | 1.17 [1.03, 1.47] | loss | 1.05 [0.81, 1.32] |
| frame/op completion p99 p50 (ms) | 6.50 [5.70, 6.80] | 7.50 [7.20, 8.70] | 6.40 [5.50, 7.00] | 1.15 [1.09, 1.36] | loss | 0.98 [0.85, 1.12] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 19.3 [18.5, 19.4] | 19.8 [19.2, 20.0] | 19.4 [18.8, 19.7] | 1.03 [0.99, 1.07] | no clear difference | 1.01 [0.97, 1.05] |
| completed-frame interval p99 p50 (ms) | 20.4 [19.1, 20.6] | 21.0 [20.8, 21.7] | 20.1 [19.3, 20.5] | 1.03 [1.01, 1.10] | no clear difference | 0.99 [0.95, 1.06] |
| GPU-process CPU per frame/op p50 (ms) | 1.83 [1.78, 1.92] | 2.06 [1.85, 2.37] | 2.10 [1.85, 2.22] | 1.13 [0.97, 1.31] | no clear difference | 1.15 [1.01, 1.23] |
| all-process CPU per frame/op p50 (ms) | 2.81 [2.75, 2.96] | 3.43 [3.08, 3.92] | 3.50 [3.17, 3.74] | 1.22 [1.08, 1.40] | loss | 1.25 [1.13, 1.34] |
| GPU-process memory p50 (MB) | 202 [200, 202] | 195 [194, 198] | 203 [201, 204] | 0.96 [0.96, 0.99] | win | 1.00 [0.99, 1.02] |
| GPU-process graphics memory p50 (MB) | 151 [148, 151] | 142 [142, 145] | 151 [148, 152] | 0.94 [0.94, 0.98] | win | 1.00 [0.98, 1.02] |
| total browser memory p50 (MB) | 363 [361, 364] | 358 [356, 361] | 364 [362, 367] | 0.99 [0.98, 1.00] | no clear difference | 1.00 [1.00, 1.01] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 75.6 | 71.9 | 1.05 [0.97, 1.11] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 117 | 85.6 | 1.37 [0.89, 1.56] | no clear difference |
| launch -> first completed job p50 (ms) | 637 | 718 | 0.89 [0.83, 1.16] | no clear difference |
| frame/op completion p50 p50 (ms) | 3.90 | 3.70 | 1.05 [0.93, 1.14] | no clear difference |
| frame/op completion p95 p50 (ms) | 6.90 | 6.20 | 1.11 [0.98, 1.44] | no clear difference |
| frame/op completion p99 p50 (ms) | 7.50 | 6.40 | 1.17 [1.06, 1.40] | loss |
| completed-frame interval p50 p50 (ms) | 16.7 | 16.7 | 1.00 [0.99, 1.01] | no clear difference |
| completed-frame interval p95 p50 (ms) | 19.8 | 19.4 | 1.02 [0.99, 1.06] | no clear difference |
| completed-frame interval p99 p50 (ms) | 21.0 | 20.1 | 1.04 [1.02, 1.10] | loss |
| GPU-process CPU per frame/op p50 (ms) | 2.06 | 2.10 | 0.98 [0.84, 1.18] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 3.43 | 3.50 | 0.98 [0.83, 1.16] | no clear difference |
| GPU-process memory p50 (MB) | 195 | 203 | 0.96 [0.95, 0.99] | win |
| GPU-process graphics memory p50 (MB) | 142 | 151 | 0.94 [0.93, 0.98] | win |
| total browser memory p50 (MB) | 358 | 364 | 0.98 [0.97, 0.99] | no clear difference |
