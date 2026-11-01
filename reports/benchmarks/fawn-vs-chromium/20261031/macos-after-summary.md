## Wins (Fawn+Doe better than stock, CI excludes 1.0)

- compute_atomic_canvas: GPU-process graphics memory p50 (MB) 0.96x [0.95, 0.99]
- heavy_particles: GPU-process graphics memory p50 (MB) 0.97x [0.96, 0.97]
- image_lab: GPU-process memory p50 (MB) 0.97x [0.96, 0.97]
- image_lab: GPU-process graphics memory p50 (MB) 0.96x [0.95, 0.97]
- magnetic_fluids: GPU-process memory p50 (MB) 0.74x [0.74, 0.79]
- magnetic_fluids: GPU-process graphics memory p50 (MB) 0.69x [0.69, 0.73]
- magnetic_fluids: total browser memory p50 (MB) 0.86x [0.85, 0.88]
- prismatic_fluids: GPU-process memory p50 (MB) 0.80x [0.79, 0.80]
- prismatic_fluids: GPU-process graphics memory p50 (MB) 0.75x [0.74, 0.75]
- prismatic_fluids: total browser memory p50 (MB) 0.89x [0.89, 0.93]

## Losses (Fawn+Doe worse than stock, CI excludes 1.0)

- compute_atomic_canvas: cold first job (nav -> first completed job) p50 (ms) 1.09x [1.02, 1.21]
- compute_atomic_canvas: launch -> first completed job p50 (ms) 1.10x [1.02, 1.17]
- compute_atomic_canvas: frame/op completion p95 p50 (ms) 1.04x [1.02, 1.06]
- cycle: cold first job (nav -> first completed job) p50 (ms) 1.32x [1.16, 1.71]
- cycle: cold first job (nav -> first completed job) p95 (ms) 1.61x [1.29, 2.17]
- cycle: launch -> first completed job p50 (ms) 1.17x [1.07, 1.35]
- cycle: frame/op completion p50 p50 (ms) 1.18x [1.06, 1.26]
- cycle: frame/op completion p95 p50 (ms) 1.04x [1.01, 1.08]
- cycle: all-process CPU per frame/op p50 (ms) 1.25x [1.11, 1.47]
- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 1.30x [1.17, 1.46]
- heavy_particles: cold first job (nav -> first completed job) p95 (ms) 3.88x [1.25, 4.14]
- heavy_particles: frame/op completion p50 p50 (ms) 1.23x [1.10, 1.31]
- image_lab: cold first job (nav -> first completed job) p50 (ms) 1.36x [1.15, 1.68]
- image_lab: cold first job (nav -> first completed job) p95 (ms) 8.08x [1.19, 8.27]
- magnetic_fluids: cold first job (nav -> first completed job) p50 (ms) 1.35x [1.08, 1.51]
- magnetic_fluids: cold first job (nav -> first completed job) p95 (ms) 1.57x [1.19, 1.75]
- prismatic_fluids: cold first job (nav -> first completed job) p50 (ms) 1.36x [1.23, 1.51]
- prismatic_fluids: cold first job (nav -> first completed job) p95 (ms) 2.01x [1.24, 2.28]
- prismatic_fluids: launch -> first completed job p50 (ms) 1.19x [1.04, 1.33]
- prismatic_fluids: frame/op completion p50 p50 (ms) 1.17x [1.05, 1.26]
- prismatic_fluids: frame/op completion p95 p50 (ms) 1.23x [1.02, 1.25]

## Per page

### compute_atomic_canvas

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 94.3 [87.2, 96.9] | 102 [97.3, 107] | 96.9 [93.7, 108] | 1.09 [1.02, 1.21] | loss | 1.03 [0.99, 1.19] |
| cold first job (nav -> first completed job) p95 (ms) | 129 [96.9, 129] | 4434 [107, 4434] | 414 [108, 414] | 34.32 [0.85, 45.62] | no clear difference | 3.21 [0.84, 4.26] |
| launch -> first completed job p50 (ms) | 546 [537, 584] | 603 [581, 639] | 647 [603, 662] | 1.10 [1.02, 1.17] | loss | 1.18 [1.04, 1.22] |
| frame/op completion p50 p50 (ms) | 35.2 [34.9, 35.5] | 36.1 [36.0, 37.0] | 35.2 [34.8, 35.5] | 1.03 [1.02, 1.05] | no clear difference | 1.00 [0.99, 1.02] |
| frame/op completion p95 p50 (ms) | 36.1 [35.6, 36.3] | 37.4 [37.0, 38.0] | 36.3 [35.9, 36.5] | 1.04 [1.02, 1.06] | loss | 1.01 [0.99, 1.02] |
| completed-frame interval p50 p50 (ms) | 49.8 [49.7, 49.9] | 49.8 [49.6, 49.9] | 49.8 [49.7, 49.9] | 1.00 [1.00, 1.00] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p95 p50 (ms) | 52.0 [51.8, 52.1] | 52.1 [51.9, 52.3] | 52.0 [51.6, 52.1] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 5.48 [5.12, 5.61] | 5.10 [4.71, 5.26] | 6.29 [5.54, 6.40] | 0.93 [0.85, 1.02] | no clear difference | 1.15 [0.99, 1.24] |
| all-process CPU per frame/op p50 (ms) | 8.60 [8.11, 8.71] | 9.76 [8.65, 10.4] | 10.5 [9.51, 10.7] | 1.13 [1.00, 1.25] | no clear difference | 1.22 [1.10, 1.32] |
| GPU-process memory p50 (MB) | 197 [193, 198] | 192 [190, 195] | 197 [195, 201] | 0.97 [0.96, 0.99] | no clear difference | 1.00 [0.99, 1.02] |
| GPU-process graphics memory p50 (MB) | 164 [163, 164] | 157 [156, 162] | 164 [164, 168] | 0.96 [0.95, 0.99] | win | 1.00 [1.00, 1.03] |
| total browser memory p50 (MB) | 361 [348, 365] | 360 [356, 364] | 365 [363, 370] | 1.00 [0.98, 1.04] | no clear difference | 1.01 [1.00, 1.06] |

### cycle

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 173 [169, 196] | 229 [225, 294] | 196 [186, 223] | 1.32 [1.16, 1.71] | loss | 1.13 [0.97, 1.30] |
| cold first job (nav -> first completed job) p95 (ms) | 263 [196, 263] | 424 [294, 424] | 364 [223, 364] | 1.61 [1.29, 2.17] | loss | 1.38 [0.88, 1.86] |
| launch -> first completed job p50 (ms) | 705 [622, 731] | 824 [779, 907] | 784 [733, 842] | 1.17 [1.07, 1.35] | loss | 1.11 [1.03, 1.34] |
| frame/op completion p50 p50 (ms) | 12.5 [11.8, 13.4] | 14.7 [13.9, 14.9] | 13.1 [11.1, 13.8] | 1.18 [1.06, 1.26] | loss | 1.05 [0.84, 1.13] |
| frame/op completion p95 p50 (ms) | 21.3 [20.6, 21.6] | 22.1 [21.8, 22.6] | 21.0 [19.8, 21.2] | 1.04 [1.01, 1.08] | loss | 0.99 [0.93, 1.02] |
| completed-frame interval p50 p50 (ms) | 18.5 [17.9, 19.4] | 20.1 [18.7, 21.0] | 18.6 [17.6, 19.4] | 1.09 [1.00, 1.15] | no clear difference | 1.01 [0.93, 1.07] |
| completed-frame interval p95 p50 (ms) | 35.4 [35.3, 35.8] | 35.7 [35.2, 36.3] | 35.5 [35.2, 35.8] | 1.01 [0.99, 1.03] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 5.56 [4.74, 5.77] | 5.95 [5.44, 6.21] | 6.15 [4.94, 6.64] | 1.07 [0.96, 1.29] | no clear difference | 1.11 [0.89, 1.38] |
| all-process CPU per frame/op p50 (ms) | 8.23 [7.01, 8.80] | 10.3 [9.46, 10.9] | 10.1 [7.87, 10.7] | 1.25 [1.11, 1.47] | loss | 1.22 [0.96, 1.48] |
| GPU-process memory p50 (MB) | 432 [432, 433] | 441 [434, 441] | 432 [432, 433] | 1.02 [1.00, 1.02] | no clear difference | 1.00 [1.00, 1.00] |
| GPU-process graphics memory p50 (MB) | 343 [343, 343] | 351 [351, 351] | 343 [343, 343] | 1.02 [1.02, 1.02] | no clear difference | 1.00 [1.00, 1.00] |
| total browser memory p50 (MB) | 637 [635, 638] | 644 [617, 645] | 637 [635, 639] | 1.01 [0.97, 1.02] | no clear difference | 1.00 [1.00, 1.00] |

### heavy_particles

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 63.9 [59.1, 70.5] | 83.0 [80.4, 90.6] | 75.4 [67.5, 78.8] | 1.30 [1.17, 1.46] | loss | 1.18 [0.99, 1.29] |
| cold first job (nav -> first completed job) p95 (ms) | 75.5 [70.5, 75.5] | 293 [90.6, 293] | 519 [78.8, 519] | 3.88 [1.25, 4.14] | loss | 6.88 [1.05, 7.34] |
| launch -> first completed job p50 (ms) | 586 [516, 599] | 635 [595, 694] | 619 [572, 654] | 1.08 [1.00, 1.24] | no clear difference | 1.06 [0.96, 1.20] |
| frame/op completion p50 p50 (ms) | 4.00 [3.80, 4.10] | 4.90 [4.40, 5.10] | 4.10 [4.00, 4.20] | 1.23 [1.10, 1.31] | loss | 1.03 [0.98, 1.08] |
| frame/op completion p95 p50 (ms) | 5.30 [4.60, 5.40] | 5.70 [4.90, 6.00] | 5.20 [4.60, 5.40] | 1.08 [0.92, 1.26] | no clear difference | 0.98 [0.85, 1.15] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.7, 16.7] | 16.7 [16.7, 16.7] | 1.00 [1.00, 1.00] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p95 p50 (ms) | 18.6 [18.6, 18.7] | 18.7 [18.6, 18.8] | 18.7 [18.7, 18.8] | 1.01 [0.99, 1.01] | no clear difference | 1.01 [1.00, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 3.87 [2.96, 3.92] | 4.10 [3.47, 4.28] | 4.10 [3.24, 4.40] | 1.06 [0.89, 1.39] | no clear difference | 1.06 [0.83, 1.39] |
| all-process CPU per frame/op p50 (ms) | 5.60 [4.20, 5.73] | 6.58 [5.33, 6.65] | 6.49 [4.99, 6.87] | 1.18 [0.95, 1.57] | no clear difference | 1.16 [0.88, 1.54] |
| GPU-process memory p50 (MB) | 276 [276, 278] | 269 [269, 270] | 276 [276, 277] | 0.98 [0.97, 0.98] | no clear difference | 1.00 [0.99, 1.00] |
| GPU-process graphics memory p50 (MB) | 223 [223, 226] | 216 [216, 217] | 223 [223, 225] | 0.97 [0.96, 0.97] | win | 1.00 [0.99, 1.01] |
| total browser memory p50 (MB) | 458 [458, 459] | 454 [451, 455] | 459 [442, 463] | 0.99 [0.98, 0.99] | no clear difference | 1.00 [0.97, 1.01] |

### image_lab

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 56.5 [45.5, 63.0] | 76.6 [68.7, 80.4] | 71.3 [59.7, 74.8] | 1.36 [1.15, 1.68] | loss | 1.26 [1.00, 1.57] |
| cold first job (nav -> first completed job) p95 (ms) | 67.4 [63.0, 67.4] | 544 [80.4, 544] | 281 [74.8, 281] | 8.08 [1.19, 8.27] | loss | 4.17 [1.11, 4.27] |
| launch -> first completed job p50 (ms) | 548 [508, 601] | 571 [544, 637] | 623 [612, 656] | 1.04 [0.93, 1.18] | no clear difference | 1.14 [1.02, 1.26] |
| frame/op completion p50 p50 (ms) | 16.5 [16.4, 16.6] | 16.5 [16.4, 16.5] | 16.5 [16.4, 16.6] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| frame/op completion p95 p50 (ms) | 18.5 [18.3, 18.6] | 18.5 [18.5, 18.7] | 18.9 [18.6, 18.9] | 1.00 [0.99, 1.01] | no clear difference | 1.02 [1.00, 1.03] |
| completed-frame interval p50 p50 (ms) | 16.6 [16.6, 16.7] | 16.6 [16.6, 16.7] | 16.7 [16.5, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.01 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.7 [18.6, 18.9] | 18.7 [18.6, 19.0] | 19.1 [18.7, 19.2] | 1.00 [0.99, 1.02] | no clear difference | 1.02 [1.00, 1.03] |
| GPU-process CPU per frame/op p50 (ms) | 4.42 [3.37, 5.03] | 4.56 [4.00, 5.06] | 5.15 [4.55, 5.32] | 1.03 [0.83, 1.46] | no clear difference | 1.17 [0.95, 1.57] |
| all-process CPU per frame/op p50 (ms) | 6.82 [5.24, 7.67] | 8.42 [6.87, 9.15] | 8.70 [7.93, 9.18] | 1.24 [0.91, 1.72] | no clear difference | 1.28 [1.05, 1.70] |
| GPU-process memory p50 (MB) | 261 [260, 261] | 252 [251, 254] | 264 [261, 265] | 0.97 [0.96, 0.97] | win | 1.01 [1.00, 1.02] |
| GPU-process graphics memory p50 (MB) | 226 [224, 226] | 217 [215, 219] | 228 [224, 230] | 0.96 [0.95, 0.97] | win | 1.01 [1.00, 1.02] |
| total browser memory p50 (MB) | 435 [434, 437] | 429 [426, 431] | 439 [429, 441] | 0.99 [0.98, 0.99] | no clear difference | 1.01 [0.98, 1.02] |

### magnetic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 80.2 [77.4, 87.5] | 108 [89.6, 120] | 90.6 [79.8, 102] | 1.35 [1.08, 1.51] | loss | 1.13 [0.98, 1.29] |
| cold first job (nav -> first completed job) p95 (ms) | 102 [87.5, 102] | 160 [120, 160] | 150 [102, 150] | 1.57 [1.19, 1.75] | loss | 1.48 [1.04, 1.65] |
| launch -> first completed job p50 (ms) | 570 [525, 603] | 642 [563, 665] | 618 [579, 642] | 1.13 [0.99, 1.24] | no clear difference | 1.08 [0.97, 1.20] |
| frame/op completion p50 p50 (ms) | 3.70 [3.00, 3.80] | 3.90 [3.10, 4.10] | 3.80 [3.50, 3.90] | 1.05 [0.84, 1.37] | no clear difference | 1.03 [0.95, 1.30] |
| frame/op completion p95 p50 (ms) | 4.80 [4.40, 5.10] | 5.40 [4.70, 5.60] | 5.10 [4.60, 5.30] | 1.12 [0.94, 1.25] | no clear difference | 1.06 [0.90, 1.18] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.7 [18.6, 18.8] | 18.7 [18.6, 18.8] | 18.7 [18.7, 18.8] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 3.33 [2.73, 3.83] | 3.88 [3.10, 4.09] | 4.15 [3.63, 4.25] | 1.16 [0.83, 1.44] | no clear difference | 1.25 [0.98, 1.53] |
| all-process CPU per frame/op p50 (ms) | 5.01 [3.93, 5.67] | 6.31 [4.92, 6.60] | 6.57 [5.65, 6.92] | 1.26 [0.92, 1.61] | no clear difference | 1.31 [1.06, 1.67] |
| GPU-process memory p50 (MB) | 247 [235, 247] | 183 [183, 186] | 245 [233, 248] | 0.74 [0.74, 0.79] | win | 0.99 [0.94, 1.04] |
| GPU-process graphics memory p50 (MB) | 207 [195, 207] | 143 [143, 143] | 205 [192, 207] | 0.69 [0.69, 0.73] | win | 0.99 [0.93, 1.06] |
| total browser memory p50 (MB) | 427 [417, 430] | 367 [366, 372] | 427 [416, 433] | 0.86 [0.85, 0.88] | win | 1.00 [0.97, 1.03] |

### prismatic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 89.5 [84.1, 94.0] | 122 [115, 128] | 108 [99.1, 117] | 1.36 [1.23, 1.51] | loss | 1.20 [1.11, 1.35] |
| cold first job (nav -> first completed job) p95 (ms) | 107 [94.0, 107] | 215 [128, 215] | 180 [117, 180] | 2.01 [1.24, 2.28] | loss | 1.68 [1.11, 1.91] |
| launch -> first completed job p50 (ms) | 571 [512, 638] | 679 [649, 694] | 646 [599, 702] | 1.19 [1.04, 1.33] | loss | 1.13 [0.99, 1.29] |
| frame/op completion p50 p50 (ms) | 3.60 [3.40, 3.70] | 4.20 [3.80, 4.40] | 3.80 [3.30, 3.80] | 1.17 [1.05, 1.26] | loss | 1.06 [0.91, 1.12] |
| frame/op completion p95 p50 (ms) | 5.30 [5.20, 5.40] | 6.50 [5.40, 6.60] | 5.20 [5.20, 5.60] | 1.23 [1.02, 1.25] | loss | 0.98 [0.96, 1.06] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.00] | no clear difference | 1.00 [0.99, 1.00] |
| completed-frame interval p95 p50 (ms) | 18.7 [18.6, 18.8] | 18.7 [18.6, 18.8] | 18.7 [18.6, 18.8] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 3.91 [2.93, 3.94] | 4.20 [3.35, 4.27] | 4.33 [3.19, 4.44] | 1.07 [0.85, 1.45] | no clear difference | 1.11 [0.81, 1.51] |
| all-process CPU per frame/op p50 (ms) | 5.62 [4.22, 5.76] | 6.62 [5.22, 6.84] | 6.85 [4.89, 6.89] | 1.18 [0.93, 1.58] | no clear difference | 1.22 [0.87, 1.62] |
| GPU-process memory p50 (MB) | 238 [237, 240] | 190 [190, 190] | 238 [238, 238] | 0.80 [0.79, 0.80] | win | 1.00 [0.99, 1.00] |
| GPU-process graphics memory p50 (MB) | 195 [195, 197] | 146 [146, 146] | 195 [195, 195] | 0.75 [0.74, 0.75] | win | 1.00 [0.99, 1.00] |
| total browser memory p50 (MB) | 422 [403, 424] | 377 [375, 377] | 423 [422, 426] | 0.89 [0.89, 0.93] | win | 1.00 [1.00, 1.05] |
