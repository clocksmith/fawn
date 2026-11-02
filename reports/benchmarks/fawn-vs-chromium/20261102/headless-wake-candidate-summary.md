Presentation mode: headless. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- heavy_particles: GPU-process graphics memory p50 (MB) 0.96x [0.95, 0.98]

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 1.25x [1.10, 1.32]
- heavy_particles: frame/op completion p50 p50 (ms) 1.21x [1.12, 1.32]

## Per page

### heavy_particles

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 62.4 [61.8, 67.8] | 77.8 [71.1, 81.9] | 74.1 [70.3, 77.6] | 1.25 [1.10, 1.32] | loss | 1.19 [1.06, 1.25] |
| cold first job (nav -> first completed job) p95 (ms) | 119 [64.6, 119] | 119 [81.4, 119] | 78.1 [76.8, 78.1] | 1.00 [0.68, 1.85] | no clear difference | 0.65 [0.64, 1.21] |
| launch -> first completed job p50 (ms) | 622 [614, 725] | 665 [633, 669] | 709 [648, 745] | 1.07 [0.90, 1.09] | no clear difference | 1.14 [0.91, 1.21] |
| frame/op completion p50 p50 (ms) | 3.80 [3.60, 4.00] | 4.60 [4.50, 4.90] | 3.70 [3.40, 3.70] | 1.21 [1.12, 1.32] | loss | 0.97 [0.89, 1.03] |
| frame/op completion p95 p50 (ms) | 5.80 [4.50, 6.40] | 5.90 [5.30, 7.30] | 4.80 [4.60, 6.20] | 1.02 [0.83, 1.52] | no clear difference | 0.83 [0.73, 1.33] |
| frame/op completion p99 p50 (ms) | 6.70 [4.90, 6.80] | 6.10 [5.80, 8.10] | 6.00 [5.30, 6.90] | 0.91 [0.87, 1.55] | no clear difference | 0.90 [0.79, 1.30] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.7, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [1.00, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.9 [18.1, 19.7] | 18.6 [18.2, 19.6] | 18.8 [18.3, 19.4] | 0.98 [0.93, 1.07] | no clear difference | 0.99 [0.93, 1.05] |
| completed-frame interval p99 p50 (ms) | 19.9 [19.1, 20.4] | 19.9 [18.6, 20.6] | 20.1 [19.1, 20.4] | 1.00 [0.93, 1.05] | no clear difference | 1.01 [0.94, 1.05] |
| GPU-process CPU per frame/op p50 (ms) | 1.89 [1.67, 2.09] | 2.45 [1.90, 2.52] | 2.06 [1.97, 2.38] | 1.29 [0.97, 1.49] | no clear difference | 1.09 [0.97, 1.32] |
| all-process CPU per frame/op p50 (ms) | 2.89 [2.58, 3.19] | 3.95 [3.10, 4.08] | 3.50 [3.31, 3.96] | 1.37 [0.98, 1.57] | no clear difference | 1.21 [1.04, 1.43] |
| GPU-process memory p50 (MB) | 202 [199, 202] | 197 [196, 199] | 203 [201, 203] | 0.98 [0.97, 1.00] | no clear difference | 1.00 [0.99, 1.02] |
| GPU-process graphics memory p50 (MB) | 151 [148, 152] | 144 [144, 146] | 151 [148, 151] | 0.96 [0.95, 0.98] | win | 1.00 [0.98, 1.02] |
| total browser memory p50 (MB) | 363 [360, 365] | 361 [358, 362] | 366 [362, 368] | 0.99 [0.98, 1.00] | no clear difference | 1.01 [1.00, 1.02] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 77.8 | 74.1 | 1.05 [0.96, 1.16] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 119 | 78.1 | 1.53 [1.04, 1.55] | loss |
| launch -> first completed job p50 (ms) | 665 | 709 | 0.94 [0.86, 1.03] | no clear difference |
| frame/op completion p50 p50 (ms) | 4.60 | 3.70 | 1.24 [1.22, 1.36] | loss |
| frame/op completion p95 p50 (ms) | 5.90 | 4.80 | 1.23 [0.85, 1.52] | no clear difference |
| frame/op completion p99 p50 (ms) | 6.10 | 6.00 | 1.02 [0.87, 1.50] | no clear difference |
| completed-frame interval p50 p50 (ms) | 16.7 | 16.7 | 1.00 [0.99, 1.00] | no clear difference |
| completed-frame interval p95 p50 (ms) | 18.6 | 18.8 | 0.99 [0.94, 1.07] | no clear difference |
| completed-frame interval p99 p50 (ms) | 19.9 | 20.1 | 0.99 [0.93, 1.08] | no clear difference |
| GPU-process CPU per frame/op p50 (ms) | 2.45 | 2.06 | 1.19 [0.85, 1.26] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 3.95 | 3.50 | 1.13 [0.84, 1.23] | no clear difference |
| GPU-process memory p50 (MB) | 197 | 203 | 0.97 [0.97, 0.98] | no clear difference |
| GPU-process graphics memory p50 (MB) | 144 | 151 | 0.96 [0.96, 0.97] | win |
| total browser memory p50 (MB) | 361 | 366 | 0.99 [0.98, 1.00] | no clear difference |
