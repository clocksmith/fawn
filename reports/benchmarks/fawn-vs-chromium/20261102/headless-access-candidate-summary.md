Presentation mode: headless. Local comparisons remain diagnostic.

## Wins (fawn_doe better than chrome, CI excludes 1.0)

- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 0.68x [0.56, 0.84]

## Losses (fawn_doe worse than chrome, CI excludes 1.0)

- heavy_particles: frame/op completion p50 p50 (ms) 1.25x [1.22, 1.34]
- heavy_particles: GPU-process CPU per frame/op p50 (ms) 1.17x [1.04, 1.78]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.30x [1.11, 1.82]

## Per page

### heavy_particles

- chrome: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_dawn: 6/6 ok, Doe mapped {'False': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- fawn_doe: 6/6 ok, Doe mapped {'True': 6}, adapter apple / metal-3 /  / , pixel hashes 6
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 138 [134, 165] | 94.8 [91.4, 113] | 82.2 [76.6, 94.3] | 0.68 [0.56, 0.84] | win | 0.59 [0.49, 0.68] |
| cold first job (nav -> first completed job) p95 (ms) | 166 [161, 166] | 188 [112, 188] | 139 [90.7, 139] | 1.13 [0.68, 1.16] | no clear difference | 0.83 [0.55, 0.86] |
| launch -> first completed job p50 (ms) | 1392 [1329, 1500] | 988 [746, 1458] | 845 [675, 1179] | 0.71 [0.54, 1.07] | no clear difference | 0.61 [0.48, 0.86] |
| frame/op completion p50 p50 (ms) | 3.20 [3.00, 3.20] | 4.00 [3.90, 4.10] | 3.20 [3.10, 3.30] | 1.25 [1.22, 1.34] | loss | 1.00 [0.97, 1.07] |
| frame/op completion p95 p50 (ms) | 4.30 [3.50, 4.50] | 5.20 [4.10, 5.40] | 4.40 [4.10, 4.70] | 1.21 [0.95, 1.49] | no clear difference | 1.02 [0.95, 1.26] |
| frame/op completion p99 p50 (ms) | 4.90 [4.10, 4.90] | 5.80 [4.40, 6.40] | 4.80 [4.60, 5.40] | 1.18 [0.90, 1.44] | no clear difference | 0.98 [0.94, 1.20] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.0 [17.5, 18.1] | 17.8 [17.4, 18.0] | 17.9 [17.7, 18.1] | 0.99 [0.97, 1.02] | no clear difference | 0.99 [0.98, 1.02] |
| completed-frame interval p99 p50 (ms) | 18.4 [17.8, 19.0] | 18.4 [17.6, 18.6] | 18.3 [18.3, 18.9] | 1.00 [0.95, 1.04] | no clear difference | 0.99 [0.96, 1.04] |
| GPU-process CPU per frame/op p50 (ms) | 1.13 [0.79, 1.27] | 1.32 [1.25, 1.58] | 1.24 [1.19, 1.39] | 1.17 [1.04, 1.78] | loss | 1.09 [0.97, 1.56] |
| all-process CPU per frame/op p50 (ms) | 1.77 [1.27, 2.01] | 2.30 [2.23, 2.78] | 2.26 [2.13, 2.44] | 1.30 [1.11, 1.82] | loss | 1.28 [1.06, 1.79] |
| GPU-process memory p50 (MB) | 202 [202, 202] | 201 [196, 202] | 203 [203, 207] | 0.99 [0.97, 1.00] | no clear difference | 1.01 [1.00, 1.02] |
| GPU-process graphics memory p50 (MB) | 151 [151, 151] | 148 [144, 149] | 152 [151, 155] | 0.98 [0.96, 0.99] | no clear difference | 1.01 [1.00, 1.03] |
| total browser memory p50 (MB) | 363 [362, 364] | 363 [359, 366] | 366 [366, 371] | 1.00 [0.99, 1.01] | no clear difference | 1.01 [1.00, 1.02] |

Candidate/control comparison: fawn_doe/fawn_dawn

| metric | fawn_doe | fawn_dawn | ratio (95% CI) | verdict |
|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 94.8 | 82.2 | 1.15 [0.98, 1.39] | no clear difference |
| cold first job (nav -> first completed job) p95 (ms) | 188 | 139 | 1.35 [0.81, 2.07] | no clear difference |
| launch -> first completed job p50 (ms) | 988 | 845 | 1.17 [0.74, 1.88] | no clear difference |
| frame/op completion p50 p50 (ms) | 4.00 | 3.20 | 1.25 [1.21, 1.32] | loss |
| frame/op completion p95 p50 (ms) | 5.20 | 4.40 | 1.18 [0.93, 1.27] | no clear difference |
| frame/op completion p99 p50 (ms) | 5.80 | 4.80 | 1.21 [0.90, 1.36] | no clear difference |
| completed-frame interval p50 p50 (ms) | 16.7 | 16.7 | 1.00 [0.99, 1.01] | no clear difference |
| completed-frame interval p95 p50 (ms) | 17.8 | 17.9 | 0.99 [0.97, 1.01] | no clear difference |
| completed-frame interval p99 p50 (ms) | 18.4 | 18.3 | 1.01 [0.96, 1.02] | no clear difference |
| GPU-process CPU per frame/op p50 (ms) | 1.32 | 1.24 | 1.07 [0.95, 1.29] | no clear difference |
| all-process CPU per frame/op p50 (ms) | 2.30 | 2.26 | 1.01 [0.93, 1.30] | no clear difference |
| GPU-process memory p50 (MB) | 201 | 203 | 0.99 [0.95, 0.99] | no clear difference |
| GPU-process graphics memory p50 (MB) | 148 | 152 | 0.97 [0.93, 0.99] | no clear difference |
| total browser memory p50 (MB) | 363 | 366 | 0.99 [0.97, 1.00] | no clear difference |
