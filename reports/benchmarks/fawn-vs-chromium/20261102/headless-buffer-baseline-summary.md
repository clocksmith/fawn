Presentation mode: headless. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- heavy_particles: GPU-process memory p50 (MB) 0.96x [0.96, 0.98]
- heavy_particles: GPU-process graphics memory p50 (MB) 0.94x [0.94, 0.97]

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 1.21x [1.09, 1.28]
- heavy_particles: cold first job (nav -> first completed job) p95 (ms) 1.15x [1.08, 1.29]
- heavy_particles: frame/op completion p50 p50 (ms) 1.06x [1.03, 1.12]
- heavy_particles: frame/op completion p95 p50 (ms) 1.44x [1.31, 1.58]
- heavy_particles: frame/op completion p99 p50 (ms) 1.18x [1.08, 1.65]
- heavy_particles: completed-frame interval p95 p50 (ms) 1.04x [1.03, 1.06]
- heavy_particles: completed-frame interval p99 p50 (ms) 1.08x [1.04, 1.09]
- heavy_particles: GPU-process CPU per frame/op p50 (ms) 1.30x [1.21, 1.44]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.37x [1.29, 1.55]

## Per page

### heavy_particles

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 60.5 [57.7, 67.1] | 73.5 [73.1, 74.3] | 70.4 [69.9, 71.3] | 1.21 [1.09, 1.28] | loss | 1.16 [1.05, 1.22] |
| cold first job (nav -> first completed job) p95 (ms) | 68.6 [61.1, 68.6] | 78.7 [74.1, 78.7] | 71.5 [70.7, 71.5] | 1.15 [1.08, 1.29] | loss | 1.04 [1.03, 1.17] |
| launch -> first completed job p50 (ms) | 582 [570, 647] | 619 [611, 720] | 629 [616, 701] | 1.06 [0.95, 1.26] | no clear difference | 1.08 [0.96, 1.22] |
| frame/op completion p50 p50 (ms) | 3.30 [3.30, 3.40] | 3.50 [3.40, 3.70] | 3.50 [3.40, 3.50] | 1.06 [1.03, 1.12] | loss | 1.06 [1.03, 1.06] |
| frame/op completion p95 p50 (ms) | 4.10 [3.80, 4.30] | 5.90 [5.50, 6.10] | 4.30 [4.30, 4.40] | 1.44 [1.31, 1.58] | loss | 1.05 [1.00, 1.16] |
| frame/op completion p99 p50 (ms) | 5.60 [4.00, 5.90] | 6.60 [6.10, 6.70] | 6.00 [5.40, 6.00] | 1.18 [1.08, 1.65] | loss | 1.07 [0.96, 1.50] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.6 [16.6, 16.6] | 16.7 [16.6, 16.7] | 0.99 [0.99, 1.00] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.4 [18.1, 18.5] | 19.2 [18.9, 19.2] | 18.4 [18.1, 18.6] | 1.04 [1.03, 1.06] | loss | 1.00 [0.98, 1.02] |
| completed-frame interval p99 p50 (ms) | 18.9 [18.9, 19.1] | 20.5 [19.6, 20.6] | 19.2 [19.0, 19.3] | 1.08 [1.04, 1.09] | loss | 1.02 [0.99, 1.02] |
| GPU-process CPU per frame/op p50 (ms) | 1.26 [1.22, 1.31] | 1.63 [1.56, 1.77] | 1.56 [1.56, 1.64] | 1.30 [1.21, 1.44] | loss | 1.24 [1.19, 1.33] |
| all-process CPU per frame/op p50 (ms) | 1.97 [1.90, 2.06] | 2.71 [2.62, 2.99] | 2.72 [2.67, 2.84] | 1.37 [1.29, 1.55] | loss | 1.38 [1.31, 1.47] |
| GPU-process memory p50 (MB) | 201 [201, 202] | 194 [194, 197] | 200 [200, 203] | 0.96 [0.96, 0.98] | win | 0.99 [0.99, 1.01] |
| GPU-process graphics memory p50 (MB) | 151 [151, 151] | 142 [142, 145] | 148 [148, 151] | 0.94 [0.94, 0.97] | win | 0.98 [0.98, 1.00] |
| total browser memory p50 (MB) | 364 [363, 365] | 357 [357, 360] | 365 [362, 367] | 0.98 [0.98, 0.99] | no clear difference | 1.00 [0.99, 1.01] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 73.5 | 70.4 | 1.04 [1.03, 1.06] | loss |
| cold first job (nav -> first completed job) p95 (ms) | 78.7 | 71.5 | 1.10 [1.04, 1.11] | loss |
| launch -> first completed job p50 (ms) | 619 | 629 | 0.98 [0.88, 1.16] | no clear difference |
| frame/op completion p50 p50 (ms) | 3.50 | 3.50 | 1.00 [0.97, 1.06] | no clear difference |
| frame/op completion p95 p50 (ms) | 5.90 | 4.30 | 1.37 [1.25, 1.42] | loss |
| frame/op completion p99 p50 (ms) | 6.60 | 6.00 | 1.10 [1.02, 1.22] | loss |
| completed-frame interval p50 p50 (ms) | 16.6 | 16.7 | 0.99 [0.99, 1.00] | no clear difference |
| completed-frame interval p95 p50 (ms) | 19.2 | 18.4 | 1.04 [1.03, 1.06] | loss |
| completed-frame interval p99 p50 (ms) | 20.5 | 19.2 | 1.07 [1.02, 1.08] | loss |
| GPU-process CPU per frame/op p50 (ms) | 1.63 | 1.56 | 1.04 [0.98, 1.14] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 2.71 | 2.72 | 1.00 [0.94, 1.11] | no clear difference |
| GPU-process memory p50 (MB) | 194 | 200 | 0.97 [0.96, 0.99] | no clear difference |
| GPU-process graphics memory p50 (MB) | 142 | 148 | 0.96 [0.94, 0.98] | win |
| total browser memory p50 (MB) | 357 | 365 | 0.98 [0.97, 0.99] | no clear difference |
