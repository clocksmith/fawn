## Wins (Fawn+Doe better than stock, CI excludes 1.0)

- heavy_particles: cold first job (nav -> first completed job) p95 (ms) 0.74x [0.69, 0.83]
- prismatic_fluids: cold first job (nav -> first completed job) p95 (ms) 0.91x [0.85, 0.93]

## Losses (Fawn+Doe worse than stock, CI excludes 1.0)

- compute_atomic_canvas: frame/op completion p50 p50 (ms) 1.05x [1.04, 1.09]
- compute_atomic_canvas: frame/op completion p95 p50 (ms) 1.07x [1.04, 1.09]
- compute_atomic_canvas: GPU-process CPU per frame/op p50 (ms) 2.25x [1.76, 2.66]
- compute_atomic_canvas: all-process CPU per frame/op p50 (ms) 2.01x [1.52, 2.38]
- compute_atomic_canvas: GPU-process memory p50 (MB) 3.72x [3.63, 3.74]
- compute_atomic_canvas: GPU-process graphics memory p50 (MB) 4.22x [4.11, 4.25]
- compute_atomic_canvas: total browser memory p50 (MB) 2.47x [2.44, 2.49]
- cycle: cold first job (nav -> first completed job) p50 (ms) 1.27x [1.10, 1.36]
- cycle: cold first job (nav -> first completed job) p95 (ms) 1.19x [1.09, 1.24]
- cycle: frame/op completion p50 p50 (ms) 1.38x [1.18, 1.44]
- cycle: frame/op completion p95 p50 (ms) 1.10x [1.03, 1.37]
- cycle: completed-frame interval p50 p50 (ms) 1.46x [1.13, 1.74]
- cycle: GPU-process CPU per frame/op p50 (ms) 2.01x [1.66, 2.38]
- cycle: all-process CPU per frame/op p50 (ms) 1.81x [1.51, 2.12]
- cycle: GPU-process memory p50 (MB) 13.26x [13.18, 13.78]
- cycle: GPU-process graphics memory p50 (MB) 16.34x [16.24, 16.96]
- cycle: total browser memory p50 (MB) 9.36x [9.27, 9.73]
- heavy_particles: frame/op completion p50 p50 (ms) 1.38x [1.34, 1.43]
- heavy_particles: frame/op completion p95 p50 (ms) 1.27x [1.23, 1.40]
- heavy_particles: GPU-process CPU per frame/op p50 (ms) 2.43x [2.08, 2.70]
- heavy_particles: all-process CPU per frame/op p50 (ms) 1.99x [1.73, 2.28]
- heavy_particles: GPU-process memory p50 (MB) 28.24x [28.16, 28.31]
- heavy_particles: GPU-process graphics memory p50 (MB) 34.26x [34.25, 34.34]
- heavy_particles: total browser memory p50 (MB) 17.49x [17.44, 17.54]
- image_lab: GPU-process CPU per frame/op p50 (ms) 2.04x [1.51, 2.37]
- image_lab: all-process CPU per frame/op p50 (ms) 1.75x [1.32, 2.05]
- image_lab: GPU-process memory p50 (MB) 4.71x [4.67, 4.75]
- image_lab: GPU-process graphics memory p50 (MB) 5.25x [5.20, 5.29]
- image_lab: total browser memory p50 (MB) 3.24x [3.21, 3.26]
- magnetic_fluids: frame/op completion p50 p50 (ms) 1.34x [1.22, 1.50]
- magnetic_fluids: frame/op completion p95 p50 (ms) 1.22x [1.19, 1.30]
- magnetic_fluids: GPU-process CPU per frame/op p50 (ms) 2.63x [2.17, 2.79]
- magnetic_fluids: all-process CPU per frame/op p50 (ms) 2.15x [1.76, 2.26]
- magnetic_fluids: GPU-process memory p50 (MB) 31.73x [31.64, 31.76]
- magnetic_fluids: GPU-process graphics memory p50 (MB) 37.26x [37.18, 37.27]
- magnetic_fluids: total browser memory p50 (MB) 18.69x [18.62, 18.75]
- prismatic_fluids: frame/op completion p50 p50 (ms) 1.42x [1.32, 1.48]
- prismatic_fluids: frame/op completion p95 p50 (ms) 1.36x [1.30, 1.42]
- prismatic_fluids: GPU-process CPU per frame/op p50 (ms) 2.19x [1.97, 2.70]
- prismatic_fluids: all-process CPU per frame/op p50 (ms) 1.79x [1.60, 2.21]
- prismatic_fluids: GPU-process memory p50 (MB) 32.69x [32.62, 32.92]
- prismatic_fluids: GPU-process graphics memory p50 (MB) 39.02x [38.95, 39.48]
- prismatic_fluids: total browser memory p50 (MB) 19.04x [18.91, 19.07]

## Per page

### compute_atomic_canvas

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , oracle 12/12, pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 103 [97.8, 121] | 112 [105, 115] | 100 [96.0, 102] | 1.08 [0.90, 1.16] | no clear difference | 0.97 [0.80, 1.03] |
| cold first job (nav -> first completed job) p95 (ms) | 138 [121, 138] | 136 [115, 136] | 112 [102, 112] | 0.99 [0.85, 1.07] | no clear difference | 0.81 [0.77, 0.89] |
| launch -> first completed job p50 (ms) | 624 [579, 714] | 603 [594, 708] | 648 [585, 656] | 0.97 [0.84, 1.15] | no clear difference | 1.04 [0.88, 1.12] |
| frame/op completion p50 p50 (ms) | 34.8 [33.8, 35.1] | 36.5 [36.4, 37.5] | 35.1 [34.3, 35.3] | 1.05 [1.04, 1.09] | loss | 1.01 [0.98, 1.04] |
| frame/op completion p95 p50 (ms) | 36.1 [35.9, 37.1] | 38.6 [38.3, 39.2] | 36.5 [35.9, 37.0] | 1.07 [1.04, 1.09] | loss | 1.01 [0.97, 1.02] |
| completed-frame interval p50 p50 (ms) | 49.6 [49.4, 49.8] | 49.8 [49.7, 49.9] | 49.5 [49.5, 49.7] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.00] |
| completed-frame interval p95 p50 (ms) | 51.6 [51.3, 51.9] | 52.5 [52.1, 53.2] | 52.1 [51.5, 52.2] | 1.02 [1.01, 1.04] | no clear difference | 1.01 [1.00, 1.02] |
| GPU-process CPU per frame/op p50 (ms) | 3.42 [3.11, 4.14] | 7.69 [7.05, 8.77] | 4.74 [3.28, 5.31] | 2.25 [1.76, 2.66] | loss | 1.39 [0.96, 1.59] |
| all-process CPU per frame/op p50 (ms) | 5.60 [5.10, 6.71] | 11.3 [10.0, 12.8] | 8.28 [5.63, 9.08] | 2.01 [1.52, 2.38] | loss | 1.48 [0.99, 1.69] |
| GPU-process memory p50 (MB) | 195 [194, 199] | 724 [722, 727] | 194 [193, 198] | 3.72 [3.63, 3.74] | loss | 0.99 [0.97, 1.02] |
| GPU-process graphics memory p50 (MB) | 164 [163, 168] | 692 [691, 697] | 164 [163, 168] | 4.22 [4.11, 4.25] | loss | 1.00 [0.97, 1.03] |
| total browser memory p50 (MB) | 361 [358, 364] | 890 [888, 892] | 360 [359, 366] | 2.47 [2.44, 2.49] | loss | 1.00 [0.99, 1.02] |

### cycle

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 183 [172, 208] | 233 [228, 236] | 189 [185, 190] | 1.27 [1.10, 1.36] | loss | 1.03 [0.90, 1.10] |
| cold first job (nav -> first completed job) p95 (ms) | 218 [208, 218] | 261 [236, 261] | 198 [190, 198] | 1.19 [1.09, 1.24] | loss | 0.91 [0.88, 0.94] |
| launch -> first completed job p50 (ms) | 756 [611, 965] | 717 [700, 776] | 681 [661, 741] | 0.95 [0.73, 1.21] | no clear difference | 0.90 [0.69, 1.15] |
| frame/op completion p50 p50 (ms) | 12.7 [12.4, 14.0] | 17.5 [15.0, 18.0] | 12.9 [12.3, 13.2] | 1.38 [1.18, 1.44] | loss | 1.02 [0.91, 1.06] |
| frame/op completion p95 p50 (ms) | 20.0 [16.1, 21.2] | 21.9 [21.8, 22.3] | 21.2 [20.4, 21.4] | 1.10 [1.03, 1.37] | loss | 1.06 [0.97, 1.32] |
| completed-frame interval p50 p50 (ms) | 17.6 [16.8, 19.1] | 25.7 [19.8, 29.9] | 18.6 [17.5, 19.1] | 1.46 [1.13, 1.74] | loss | 1.06 [0.95, 1.11] |
| completed-frame interval p95 p50 (ms) | 34.6 [23.1, 35.6] | 35.6 [35.3, 36.0] | 35.3 [34.6, 35.6] | 1.03 [0.99, 1.54] | no clear difference | 1.02 [0.98, 1.54] |
| GPU-process CPU per frame/op p50 (ms) | 3.23 [2.80, 3.71] | 6.50 [6.15, 6.71] | 3.51 [2.89, 3.62] | 2.01 [1.66, 2.38] | loss | 1.09 [0.85, 1.28] |
| all-process CPU per frame/op p50 (ms) | 4.94 [4.30, 5.70] | 8.94 [8.32, 9.21] | 5.96 [4.78, 6.28] | 1.81 [1.51, 2.12] | loss | 1.21 [0.93, 1.43] |
| GPU-process memory p50 (MB) | 430 [429, 431] | 5701 [5667, 5916] | 430 [430, 430] | 13.26 [13.18, 13.78] | loss | 1.00 [1.00, 1.00] |
| GPU-process graphics memory p50 (MB) | 343 [343, 343] | 5609 [5575, 5820] | 343 [343, 343] | 16.34 [16.24, 16.96] | loss | 1.00 [1.00, 1.00] |
| total browser memory p50 (MB) | 631 [629, 633] | 5901 [5848, 6122] | 632 [630, 633] | 9.36 [9.27, 9.73] | loss | 1.00 [1.00, 1.01] |

### heavy_particles

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 80.7 [60.3, 102] | 79.0 [77.4, 79.6] | 68.7 [66.2, 71.4] | 0.98 [0.77, 1.32] | no clear difference | 0.85 [0.67, 1.14] |
| cold first job (nav -> first completed job) p95 (ms) | 115 [102, 115] | 84.8 [79.6, 84.8] | 75.7 [71.4, 75.7] | 0.74 [0.69, 0.83] | win | 0.66 [0.62, 0.74] |
| launch -> first completed job p50 (ms) | 604 [574, 923] | 592 [555, 621] | 620 [592, 634] | 0.98 [0.63, 1.06] | no clear difference | 1.03 [0.66, 1.08] |
| frame/op completion p50 p50 (ms) | 4.00 [3.90, 4.10] | 5.50 [5.40, 5.70] | 3.90 [3.80, 4.00] | 1.38 [1.34, 1.43] | loss | 0.98 [0.95, 1.00] |
| frame/op completion p95 p50 (ms) | 5.10 [5.00, 5.20] | 6.50 [6.40, 7.10] | 5.20 [5.10, 5.20] | 1.27 [1.23, 1.40] | loss | 1.02 [0.98, 1.04] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.7, 16.7] | 1.00 [0.99, 1.00] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p95 p50 (ms) | 18.7 [18.6, 18.8] | 18.6 [18.5, 18.7] | 18.7 [18.6, 18.8] | 0.99 [0.98, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 2.28 [2.07, 2.55] | 5.52 [5.17, 5.96] | 2.48 [2.21, 2.68] | 2.43 [2.08, 2.70] | loss | 1.09 [0.92, 1.27] |
| all-process CPU per frame/op p50 (ms) | 3.34 [3.03, 3.70] | 6.63 [6.22, 7.10] | 3.86 [3.51, 4.22] | 1.99 [1.73, 2.28] | loss | 1.16 [1.01, 1.36] |
| GPU-process memory p50 (MB) | 276 [275, 277] | 7788 [7786, 7802] | 276 [273, 276] | 28.24 [28.16, 28.31] | loss | 1.00 [0.99, 1.00] |
| GPU-process graphics memory p50 (MB) | 226 [226, 226] | 7729 [7728, 7744] | 225 [223, 226] | 34.26 [34.25, 34.34] | loss | 1.00 [0.99, 1.00] |
| total browser memory p50 (MB) | 455 [455, 457] | 7968 [7967, 7982] | 455 [453, 455] | 17.49 [17.44, 17.54] | loss | 1.00 [0.99, 1.00] |

### image_lab

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 56.5 [53.5, 85.0] | 71.6 [69.5, 77.0] | 56.7 [53.8, 62.5] | 1.27 [0.82, 1.38] | no clear difference | 1.00 [0.64, 1.16] |
| cold first job (nav -> first completed job) p95 (ms) | 111 [85.0, 111] | 87.2 [77.0, 87.2] | 74.3 [62.5, 74.3] | 0.79 [0.69, 1.02] | no clear difference | 0.67 [0.56, 0.86] |
| launch -> first completed job p50 (ms) | 582 [559, 872] | 561 [546, 632] | 569 [533, 608] | 0.96 [0.63, 1.11] | no clear difference | 0.98 [0.62, 1.07] |
| frame/op completion p50 p50 (ms) | 16.6 [16.5, 16.6] | 16.6 [16.6, 16.6] | 16.6 [16.5, 16.6] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| frame/op completion p95 p50 (ms) | 18.5 [18.4, 19.0] | 18.0 [17.6, 18.2] | 18.6 [18.3, 18.7] | 0.97 [0.94, 0.99] | no clear difference | 1.01 [0.97, 1.02] |
| completed-frame interval p50 p50 (ms) | 16.6 [16.6, 16.7] | 16.7 [16.7, 16.7] | 16.6 [16.6, 16.7] | 1.01 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.8 [18.6, 19.2] | 18.3 [18.1, 18.4] | 18.8 [18.6, 18.8] | 0.97 [0.95, 0.98] | no clear difference | 1.00 [0.97, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 2.63 [2.30, 3.13] | 5.37 [4.48, 5.61] | 2.75 [2.65, 3.09] | 2.04 [1.51, 2.37] | loss | 1.05 [0.85, 1.28] |
| all-process CPU per frame/op p50 (ms) | 4.20 [3.60, 4.92] | 7.36 [6.39, 7.65] | 4.80 [4.66, 5.38] | 1.75 [1.32, 2.05] | loss | 1.14 [0.96, 1.44] |
| GPU-process memory p50 (MB) | 259 [258, 261] | 1220 [1217, 1223] | 259 [258, 260] | 4.71 [4.67, 4.75] | loss | 1.00 [0.99, 1.01] |
| GPU-process graphics memory p50 (MB) | 226 [224, 228] | 1186 [1183, 1188] | 226 [225, 228] | 5.25 [5.20, 5.29] | loss | 1.00 [0.99, 1.01] |
| total browser memory p50 (MB) | 431 [429, 432] | 1395 [1383, 1399] | 434 [432, 436] | 3.24 [3.21, 3.26] | loss | 1.01 [1.00, 1.01] |

### magnetic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 87.5 [76.8, 118] | 93.8 [90.8, 95.8] | 85.5 [76.8, 89.6] | 1.07 [0.79, 1.24] | no clear difference | 0.98 [0.66, 1.14] |
| cold first job (nav -> first completed job) p95 (ms) | 127 [118, 127] | 126 [95.8, 126] | 98.8 [89.6, 98.8] | 0.99 [0.75, 1.06] | no clear difference | 0.78 [0.72, 0.83] |
| launch -> first completed job p50 (ms) | 584 [517, 894] | 630 [565, 643] | 613 [556, 626] | 1.08 [0.66, 1.22] | no clear difference | 1.05 [0.63, 1.18] |
| frame/op completion p50 p50 (ms) | 2.90 [2.60, 3.20] | 3.90 [3.80, 3.90] | 2.70 [2.70, 2.80] | 1.34 [1.22, 1.50] | loss | 0.93 [0.84, 1.08] |
| frame/op completion p95 p50 (ms) | 4.50 [4.30, 4.50] | 5.50 [5.40, 5.70] | 4.30 [4.20, 4.40] | 1.22 [1.19, 1.30] | loss | 0.96 [0.93, 1.00] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p95 p50 (ms) | 18.8 [18.7, 19.0] | 18.6 [18.4, 18.7] | 18.9 [18.9, 19.0] | 0.99 [0.97, 1.00] | no clear difference | 1.01 [0.99, 1.02] |
| GPU-process CPU per frame/op p50 (ms) | 2.12 [2.03, 2.56] | 5.57 [5.42, 5.68] | 2.51 [2.32, 2.60] | 2.63 [2.17, 2.79] | loss | 1.18 [0.97, 1.26] |
| all-process CPU per frame/op p50 (ms) | 3.16 [3.03, 3.79] | 6.80 [6.54, 6.86] | 4.05 [3.78, 4.15] | 2.15 [1.76, 2.26] | loss | 1.28 [1.06, 1.37] |
| GPU-process memory p50 (MB) | 244 [244, 245] | 7755 [7742, 7756] | 245 [232, 247] | 31.73 [31.64, 31.76] | loss | 1.00 [0.95, 1.01] |
| GPU-process graphics memory p50 (MB) | 207 [207, 207] | 7712 [7697, 7712] | 207 [195, 209] | 37.26 [37.18, 37.27] | loss | 1.00 [0.94, 1.01] |
| total browser memory p50 (MB) | 425 [423, 426] | 7936 [7922, 7937] | 414 [411, 427] | 18.69 [18.62, 18.75] | loss | 0.97 [0.97, 1.01] |

### prismatic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter apple / metal-3 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 126 [97.5, 132] | 113 [107, 118] | 94.1 [91.8, 99.0] | 0.90 [0.83, 1.19] | no clear difference | 0.75 [0.71, 1.00] |
| cold first job (nav -> first completed job) p95 (ms) | 139 [132, 139] | 127 [118, 127] | 112 [99.0, 112] | 0.91 [0.85, 0.93] | win | 0.81 [0.72, 0.82] |
| launch -> first completed job p50 (ms) | 834 [557, 871] | 649 [582, 668] | 598 [582, 671] | 0.78 [0.68, 1.18] | no clear difference | 0.72 [0.68, 1.15] |
| frame/op completion p50 p50 (ms) | 3.30 [3.20, 3.40] | 4.70 [4.40, 4.80] | 3.50 [3.30, 3.50] | 1.42 [1.32, 1.48] | loss | 1.06 [0.97, 1.09] |
| frame/op completion p95 p50 (ms) | 5.00 [4.80, 5.10] | 6.80 [6.50, 6.80] | 5.10 [5.00, 5.10] | 1.36 [1.30, 1.42] | loss | 1.02 [0.98, 1.06] |
| completed-frame interval p50 p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.00] | no clear difference | 1.00 [0.99, 1.00] |
| completed-frame interval p95 p50 (ms) | 18.8 [18.6, 18.8] | 18.5 [18.5, 18.6] | 18.8 [18.6, 18.9] | 0.98 [0.98, 1.00] | no clear difference | 1.00 [0.99, 1.02] |
| GPU-process CPU per frame/op p50 (ms) | 2.57 [2.09, 2.75] | 5.63 [5.35, 5.72] | 2.54 [2.49, 3.19] | 2.19 [1.97, 2.70] | loss | 0.99 [0.91, 1.40] |
| all-process CPU per frame/op p50 (ms) | 3.77 [3.06, 4.09] | 6.76 [6.35, 6.91] | 4.05 [3.92, 5.05] | 1.79 [1.60, 2.21] | loss | 1.07 [0.98, 1.52] |
| GPU-process memory p50 (MB) | 237 [235, 237] | 7747 [7732, 7762] | 236 [235, 238] | 32.69 [32.62, 32.92] | loss | 1.00 [0.99, 1.00] |
| GPU-process graphics memory p50 (MB) | 197 [195, 197] | 7700 [7685, 7715] | 195 [195, 197] | 39.02 [38.95, 39.48] | loss | 0.99 [0.99, 1.00] |
| total browser memory p50 (MB) | 417 [416, 419] | 7930 [7913, 7943] | 420 [419, 422] | 19.04 [18.91, 19.07] | loss | 1.01 [1.00, 1.01] |
