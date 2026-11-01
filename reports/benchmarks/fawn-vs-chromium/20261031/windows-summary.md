## Wins (Fawn+Doe better than stock, CI excludes 1.0)

- image_lab: frame/op completion p99 p50 (ms) 0.90x [0.88, 0.97]
- magnetic_fluids: cold first job (nav -> first completed job) p95 (ms) 0.78x [0.74, 0.82]
- prismatic_fluids: cold first job (nav -> first completed job) p95 (ms) 0.60x [0.59, 0.80]

## Losses (Fawn+Doe worse than stock, CI excludes 1.0)

- compute_atomic_canvas: cold first job (nav -> first completed job) p50 (ms) 1.09x [1.06, 1.12]
- compute_atomic_canvas: cold first job (nav -> first completed job) p95 (ms) 1.12x [1.07, 1.13]
- compute_atomic_canvas: launch -> first completed job p50 (ms) 1.11x [1.08, 1.13]
- compute_atomic_canvas: GPU-process CPU per frame/op p50 (ms) 2.02x [1.91, 2.09]
- compute_atomic_canvas: all-process CPU per frame/op p50 (ms) 1.42x [1.36, 1.55]
- compute_atomic_canvas: GPU-process memory p50 (MB) 2.70x [2.69, 2.71]
- compute_atomic_canvas: GPU-process graphics memory p50 (MB) 3.19x [3.18, 3.20]
- compute_atomic_canvas: total browser memory p50 (MB) 1.81x [1.80, 1.83]
- cycle: cold first job (nav -> first completed job) p50 (ms) 1.45x [1.35, 1.54]
- cycle: cold first job (nav -> first completed job) p95 (ms) 1.10x [1.09, 1.44]
- cycle: launch -> first completed job p50 (ms) 1.39x [1.11, 1.47]
- cycle: frame/op completion p50 p50 (ms) 1.21x [1.15, 1.31]
- cycle: frame/op completion p95 p50 (ms) 1.07x [1.06, 1.08]
- cycle: frame/op completion p99 p50 (ms) 1.06x [1.04, 1.10]
- cycle: completed-frame interval p50 p50 (ms) 1.22x [1.21, 1.23]
- cycle: completed-frame interval p95 p50 (ms) 1.18x [1.17, 1.20]
- cycle: completed-frame interval p99 p50 (ms) 1.11x [1.03, 1.19]
- cycle: GPU-process CPU per frame/op p50 (ms) 2.33x [2.23, 2.50]
- cycle: all-process CPU per frame/op p50 (ms) 2.26x [2.05, 2.50]
- cycle: GPU-process memory p50 (MB) 10.63x [10.52, 10.80]
- cycle: GPU-process graphics memory p50 (MB) 16.51x [16.38, 16.72]
- cycle: total browser memory p50 (MB) 6.70x [6.64, 6.76]
- heavy_particles: cold first job (nav -> first completed job) p50 (ms) 1.15x [1.08, 1.41]
- heavy_particles: frame/op completion p50 p50 (ms) 3.91x [3.59, 4.09]
- heavy_particles: frame/op completion p95 p50 (ms) 3.93x [3.90, 4.12]
- heavy_particles: frame/op completion p99 p50 (ms) 7.23x [6.54, 7.77]
- heavy_particles: completed-frame interval p50 p50 (ms) 1.11x [1.08, 1.14]
- heavy_particles: completed-frame interval p95 p50 (ms) 1.08x [1.06, 1.13]
- heavy_particles: completed-frame interval p99 p50 (ms) 2.45x [2.29, 2.54]
- heavy_particles: GPU-process CPU per frame/op p50 (ms) 2.84x [2.79, 2.93]
- heavy_particles: all-process CPU per frame/op p50 (ms) 2.57x [2.06, 2.90]
- heavy_particles: GPU-process memory p50 (MB) 27.07x [25.49, 27.39]
- heavy_particles: GPU-process graphics memory p50 (MB) 49.65x [48.12, 50.23]
- heavy_particles: total browser memory p50 (MB) 15.12x [14.52, 15.34]
- image_lab: cold first job (nav -> first completed job) p50 (ms) 1.23x [1.18, 1.25]
- image_lab: frame/op completion p50 p50 (ms) 1.05x [1.04, 1.05]
- image_lab: completed-frame interval p50 p50 (ms) 1.07x [1.05, 1.07]
- image_lab: GPU-process CPU per frame/op p50 (ms) 2.02x [1.95, 2.18]
- image_lab: all-process CPU per frame/op p50 (ms) 1.92x [1.79, 2.03]
- image_lab: GPU-process memory p50 (MB) 3.82x [3.58, 3.87]
- image_lab: GPU-process graphics memory p50 (MB) 4.10x [4.08, 4.16]
- image_lab: total browser memory p50 (MB) 2.64x [2.48, 2.66]
- magnetic_fluids: frame/op completion p50 p50 (ms) 1.84x [1.41, 2.69]
- magnetic_fluids: frame/op completion p95 p50 (ms) 3.65x [3.57, 3.65]
- magnetic_fluids: frame/op completion p99 p50 (ms) 7.72x [5.73, 8.24]
- magnetic_fluids: completed-frame interval p50 p50 (ms) 1.07x [1.03, 1.08]
- magnetic_fluids: completed-frame interval p99 p50 (ms) 2.57x [2.31, 2.79]
- magnetic_fluids: GPU-process CPU per frame/op p50 (ms) 3.02x [2.90, 3.08]
- magnetic_fluids: all-process CPU per frame/op p50 (ms) 2.71x [2.60, 2.75]
- magnetic_fluids: GPU-process memory p50 (MB) 31.04x [29.94, 31.79]
- magnetic_fluids: GPU-process graphics memory p50 (MB) 61.63x [60.73, 62.44]
- magnetic_fluids: total browser memory p50 (MB) 16.60x [16.00, 17.08]
- prismatic_fluids: frame/op completion p50 p50 (ms) 1.64x [1.51, 1.72]
- prismatic_fluids: frame/op completion p95 p50 (ms) 1.75x [1.68, 2.06]
- prismatic_fluids: frame/op completion p99 p50 (ms) 3.33x [2.93, 3.75]
- prismatic_fluids: completed-frame interval p50 p50 (ms) 1.10x [1.08, 1.11]
- prismatic_fluids: completed-frame interval p95 p50 (ms) 1.29x [1.27, 1.30]
- prismatic_fluids: completed-frame interval p99 p50 (ms) 2.31x [2.03, 2.50]
- prismatic_fluids: GPU-process CPU per frame/op p50 (ms) 2.97x [2.94, 3.04]
- prismatic_fluids: all-process CPU per frame/op p50 (ms) 2.73x [2.70, 2.84]
- prismatic_fluids: GPU-process memory p50 (MB) 28.15x [27.67, 28.88]
- prismatic_fluids: GPU-process graphics memory p50 (MB) 56.68x [55.31, 57.21]
- prismatic_fluids: total browser memory p50 (MB) 15.25x [15.08, 15.58]

## Per page

### compute_atomic_canvas

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , oracle 12/12, pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , oracle 12/12, pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , oracle 12/12, pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 928 [902, 947] | 1011 [1002, 1032] | 914 [907, 924] | 1.09 [1.06, 1.12] | loss | 0.99 [0.96, 1.01] |
| cold first job (nav -> first completed job) p95 (ms) | 967 [947, 967] | 1082 [1032, 1082] | 993 [924, 993] | 1.12 [1.07, 1.13] | loss | 1.03 [0.96, 1.04] |
| launch -> first completed job p50 (ms) | 1257 [1252, 1275] | 1393 [1372, 1416] | 1271 [1267, 1286] | 1.11 [1.08, 1.13] | loss | 1.01 [1.00, 1.02] |
| frame/op completion p50 p50 (ms) | 517 [517, 518] | 508 [508, 508] | 518 [518, 518] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| frame/op completion p95 p50 (ms) | 520 [520, 520] | 510 [509, 511] | 520 [520, 521] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| frame/op completion p99 p50 (ms) | 520 [520, 521] | 510 [510, 511] | 522 [521, 522] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p50 p50 (ms) | 520 [520, 521] | 515 [515, 515] | 522 [521, 522] | 0.99 [0.99, 0.99] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p95 p50 (ms) | 528 [528, 529] | 517 [516, 517] | 529 [528, 529] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| completed-frame interval p99 p50 (ms) | 529 [528, 530] | 517 [517, 518] | 530 [529, 531] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| GPU-process CPU per frame/op p50 (ms) | 10.1 [9.88, 10.5] | 20.5 [20.0, 20.8] | 15.5 [15.0, 16.3] | 2.02 [1.91, 2.09] | loss | 1.53 [1.44, 1.62] |
| all-process CPU per frame/op p50 (ms) | 21.7 [21.1, 22.2] | 30.7 [30.0, 33.3] | 36.7 [36.2, 37.7] | 1.42 [1.36, 1.55] | loss | 1.69 [1.64, 1.78] |
| GPU-process memory p50 (MB) | 152 [151, 152] | 409 [409, 410] | 153 [152, 153] | 2.70 [2.69, 2.71] | loss | 1.01 [1.00, 1.01] |
| GPU-process graphics memory p50 (MB) | 85.8 [85.8, 86.1] | 274 [274, 274] | 85.8 [85.8, 85.8] | 3.19 [3.18, 3.20] | loss | 1.00 [1.00, 1.00] |
| total browser memory p50 (MB) | 315 [313, 317] | 572 [570, 574] | 316 [315, 320] | 1.81 [1.80, 1.83] | loss | 1.00 [1.00, 1.02] |

### cycle

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 1079 [1018, 1148] | 1564 [1552, 1596] | 1069 [1037, 1078] | 1.45 [1.35, 1.54] | loss | 0.99 [0.92, 1.05] |
| cold first job (nav -> first completed job) p95 (ms) | 1513 [1148, 1513] | 1658 [1596, 1658] | 1108 [1078, 1108] | 1.10 [1.09, 1.44] | loss | 0.73 [0.72, 0.97] |
| launch -> first completed job p50 (ms) | 1399 [1374, 1749] | 1944 [1912, 2044] | 1447 [1421, 1460] | 1.39 [1.11, 1.47] | loss | 1.03 [0.83, 1.06] |
| frame/op completion p50 p50 (ms) | 14.0 [13.0, 14.7] | 17.0 [16.9, 17.1] | 15.0 [14.9, 15.0] | 1.21 [1.15, 1.31] | loss | 1.07 [1.02, 1.15] |
| frame/op completion p95 p50 (ms) | 18.0 [17.9, 18.1] | 19.3 [19.2, 19.5] | 18.1 [18.1, 18.4] | 1.07 [1.06, 1.08] | loss | 1.01 [1.00, 1.02] |
| frame/op completion p99 p50 (ms) | 19.0 [18.9, 19.1] | 20.1 [19.8, 20.9] | 19.1 [19.0, 19.4] | 1.06 [1.04, 1.10] | loss | 1.01 [0.99, 1.03] |
| completed-frame interval p50 p50 (ms) | 19.1 [19.1, 19.2] | 23.3 [23.2, 23.4] | 19.3 [19.2, 19.4] | 1.22 [1.21, 1.23] | loss | 1.01 [1.00, 1.02] |
| completed-frame interval p95 p50 (ms) | 22.0 [21.7, 22.2] | 25.9 [25.9, 26.3] | 22.3 [22.2, 22.5] | 1.18 [1.17, 1.20] | loss | 1.01 [1.00, 1.03] |
| completed-frame interval p99 p50 (ms) | 24.6 [23.2, 26.3] | 27.3 [27.0, 27.8] | 26.0 [23.2, 27.6] | 1.11 [1.03, 1.19] | loss | 1.06 [0.91, 1.16] |
| GPU-process CPU per frame/op p50 (ms) | 2.81 [2.78, 2.93] | 6.55 [6.47, 7.01] | 3.64 [3.63, 3.67] | 2.33 [2.23, 2.50] | loss | 1.29 [1.24, 1.31] |
| all-process CPU per frame/op p50 (ms) | 5.43 [5.13, 5.95] | 12.3 [12.1, 13.0] | 7.34 [7.21, 7.55] | 2.26 [2.05, 2.50] | loss | 1.35 [1.22, 1.46] |
| GPU-process memory p50 (MB) | 289 [284, 290] | 3073 [3047, 3080] | 291 [290, 291] | 10.63 [10.52, 10.80] | loss | 1.00 [1.00, 1.02] |
| GPU-process graphics memory p50 (MB) | 191 [188, 191] | 3151 [3125, 3170] | 191 [191, 191] | 16.51 [16.38, 16.72] | loss | 1.00 [1.00, 1.01] |
| total browser memory p50 (MB) | 487 [483, 490] | 3268 [3246, 3277] | 488 [485, 490] | 6.70 [6.64, 6.76] | loss | 1.00 [0.99, 1.01] |

### heavy_particles

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 582 [485, 608] | 669 [654, 728] | 519 [512, 541] | 1.15 [1.08, 1.41] | loss | 0.89 [0.85, 1.08] |
| cold first job (nav -> first completed job) p95 (ms) | 960 [608, 960] | 761 [728, 761] | 654 [541, 654] | 0.79 [0.78, 1.24] | no clear difference | 0.68 [0.58, 1.02] |
| launch -> first completed job p50 (ms) | 935 [835, 1212] | 1052 [1026, 1087] | 936 [913, 1003] | 1.13 [0.85, 1.27] | no clear difference | 1.00 [0.76, 1.12] |
| frame/op completion p50 p50 (ms) | 3.50 [3.40, 3.70] | 13.7 [13.0, 14.3] | 3.80 [3.80, 3.80] | 3.91 [3.59, 4.09] | loss | 1.09 [1.03, 1.12] |
| frame/op completion p95 p50 (ms) | 4.10 [4.10, 4.10] | 16.1 [16.0, 16.9] | 4.10 [4.00, 4.10] | 3.93 [3.90, 4.12] | loss | 1.00 [0.98, 1.00] |
| frame/op completion p99 p50 (ms) | 4.40 [4.30, 4.60] | 31.8 [29.4, 33.4] | 4.80 [4.70, 4.90] | 7.23 [6.54, 7.77] | loss | 1.09 [1.02, 1.14] |
| completed-frame interval p50 p50 (ms) | 9.60 [9.50, 9.70] | 10.7 [10.4, 10.9] | 9.40 [9.30, 9.40] | 1.11 [1.08, 1.14] | loss | 0.98 [0.96, 0.99] |
| completed-frame interval p95 p50 (ms) | 11.0 [10.9, 11.1] | 11.9 [11.7, 12.4] | 11.0 [10.9, 11.1] | 1.08 [1.06, 1.13] | loss | 1.00 [0.99, 1.01] |
| completed-frame interval p99 p50 (ms) | 11.2 [11.2, 11.4] | 27.4 [25.6, 28.4] | 11.5 [11.3, 11.6] | 2.45 [2.29, 2.54] | loss | 1.03 [1.00, 1.04] |
| GPU-process CPU per frame/op p50 (ms) | 1.73 [1.72, 1.75] | 4.90 [4.83, 5.05] | 2.18 [2.17, 2.27] | 2.84 [2.79, 2.93] | loss | 1.26 [1.25, 1.32] |
| all-process CPU per frame/op p50 (ms) | 2.55 [2.52, 3.17] | 6.57 [6.47, 7.37] | 3.65 [3.53, 4.24] | 2.57 [2.06, 2.90] | loss | 1.43 [1.14, 1.67] |
| GPU-process memory p50 (MB) | 206 [205, 213] | 5581 [5377, 5629] | 214 [209, 216] | 27.07 [25.49, 27.39] | loss | 1.04 [1.00, 1.05] |
| GPU-process graphics memory p50 (MB) | 121 [121, 121] | 5999 [5815, 6070] | 121 [121, 121] | 49.65 [48.12, 50.23] | loss | 1.00 [1.00, 1.00] |
| total browser memory p50 (MB) | 380 [377, 385] | 5753 [5545, 5800] | 383 [382, 390] | 15.12 [14.52, 15.34] | loss | 1.01 [1.00, 1.03] |

### image_lab

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 1
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 1
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , pixel hashes 1
- identical pixel hashes across arms: True

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 576 [567, 595] | 709 [699, 712] | 573 [566, 579] | 1.23 [1.18, 1.25] | loss | 0.99 [0.96, 1.02] |
| cold first job (nav -> first completed job) p95 (ms) | 957 [595, 957] | 747 [712, 747] | 596 [579, 596] | 0.78 [0.75, 1.24] | no clear difference | 0.62 [0.61, 1.00] |
| launch -> first completed job p50 (ms) | 925 [894, 1320] | 1075 [1053, 1165] | 951 [925, 982] | 1.16 [0.81, 1.30] | no clear difference | 1.03 [0.71, 1.09] |
| frame/op completion p50 p50 (ms) | 9.10 [9.10, 9.20] | 9.60 [9.50, 9.60] | 9.10 [9.00, 9.10] | 1.05 [1.04, 1.05] | loss | 1.00 [0.99, 1.00] |
| frame/op completion p95 p50 (ms) | 10.9 [10.9, 11.1] | 11.1 [10.9, 11.1] | 10.6 [10.1, 11.0] | 1.02 [0.99, 1.02] | no clear difference | 0.97 [0.91, 1.01] |
| frame/op completion p99 p50 (ms) | 12.5 [11.7, 12.9] | 11.3 [11.2, 11.5] | 12.6 [12.0, 13.4] | 0.90 [0.88, 0.97] | win | 1.01 [0.95, 1.13] |
| completed-frame interval p50 p50 (ms) | 9.20 [9.20, 9.20] | 9.80 [9.70, 9.80] | 9.20 [9.20, 9.20] | 1.07 [1.05, 1.07] | loss | 1.00 [1.00, 1.00] |
| completed-frame interval p95 p50 (ms) | 11.0 [10.9, 11.2] | 11.3 [11.1, 11.3] | 11.0 [10.2, 11.2] | 1.03 [1.00, 1.04] | no clear difference | 1.00 [0.93, 1.02] |
| completed-frame interval p99 p50 (ms) | 11.8 [11.2, 12.2] | 11.5 [11.4, 11.6] | 12.4 [11.9, 12.7] | 0.97 [0.94, 1.03] | no clear difference | 1.05 [0.99, 1.11] |
| GPU-process CPU per frame/op p50 (ms) | 2.36 [2.18, 2.43] | 4.76 [4.72, 4.76] | 2.66 [2.64, 2.71] | 2.02 [1.95, 2.18] | loss | 1.13 [1.09, 1.24] |
| all-process CPU per frame/op p50 (ms) | 3.89 [3.68, 4.16] | 7.45 [7.40, 7.47] | 5.42 [5.32, 5.47] | 1.92 [1.79, 2.03] | loss | 1.39 [1.29, 1.48] |
| GPU-process memory p50 (MB) | 224 [222, 226] | 856 [803, 859] | 224 [223, 225] | 3.82 [3.58, 3.87] | loss | 1.00 [0.99, 1.01] |
| GPU-process graphics memory p50 (MB) | 121 [119, 121] | 496 [494, 496] | 121 [120, 121] | 4.10 [4.08, 4.16] | loss | 1.00 [0.99, 1.02] |
| total browser memory p50 (MB) | 386 [385, 390] | 1020 [967, 1024] | 388 [385, 389] | 2.64 [2.48, 2.66] | loss | 1.00 [0.99, 1.01] |

### magnetic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 606 [578, 961] | 716 [709, 740] | 562 [551, 575] | 1.18 [0.74, 1.25] | no clear difference | 0.93 [0.58, 0.98] |
| cold first job (nav -> first completed job) p95 (ms) | 1005 [961, 1005] | 786 [740, 786] | 623 [575, 623] | 0.78 [0.74, 0.82] | win | 0.62 [0.57, 0.65] |
| launch -> first completed job p50 (ms) | 937 [904, 2363] | 1090 [1071, 1124] | 1021 [940, 1041] | 1.16 [0.46, 1.23] | no clear difference | 1.09 [0.40, 1.15] |
| frame/op completion p50 p50 (ms) | 3.70 [3.60, 3.80] | 6.80 [5.20, 9.70] | 3.70 [3.70, 3.80] | 1.84 [1.41, 2.69] | loss | 1.00 [0.97, 1.06] |
| frame/op completion p95 p50 (ms) | 4.00 [4.00, 4.00] | 14.6 [14.3, 14.6] | 4.00 [4.00, 4.00] | 3.65 [3.57, 3.65] | loss | 1.00 [1.00, 1.00] |
| frame/op completion p99 p50 (ms) | 4.30 [4.20, 4.50] | 33.2 [25.2, 34.8] | 4.70 [4.70, 4.80] | 7.72 [5.73, 8.24] | loss | 1.09 [1.04, 1.14] |
| completed-frame interval p50 p50 (ms) | 9.60 [9.60, 10.0] | 10.3 [10.1, 10.4] | 9.40 [9.40, 9.80] | 1.07 [1.03, 1.08] | loss | 0.98 [0.94, 1.02] |
| completed-frame interval p95 p50 (ms) | 11.0 [11.0, 11.1] | 11.3 [11.3, 11.4] | 11.0 [11.0, 11.1] | 1.03 [1.02, 1.04] | no clear difference | 1.00 [0.99, 1.01] |
| completed-frame interval p99 p50 (ms) | 11.2 [11.2, 11.2] | 28.8 [25.9, 31.3] | 11.4 [11.3, 11.5] | 2.57 [2.31, 2.79] | loss | 1.02 [1.01, 1.03] |
| GPU-process CPU per frame/op p50 (ms) | 1.57 [1.56, 1.63] | 4.76 [4.67, 4.81] | 2.13 [2.08, 2.15] | 3.02 [2.90, 3.08] | loss | 1.36 [1.30, 1.37] |
| all-process CPU per frame/op p50 (ms) | 2.37 [2.34, 2.45] | 6.42 [6.34, 6.46] | 3.54 [3.49, 3.58] | 2.71 [2.60, 2.75] | loss | 1.49 [1.44, 1.53] |
| GPU-process memory p50 (MB) | 185 [183, 187] | 5749 [5577, 5851] | 189 [188, 191] | 31.04 [29.94, 31.79] | loss | 1.02 [1.01, 1.04] |
| GPU-process graphics memory p50 (MB) | 100 [99.9, 101] | 6184 [6108, 6243] | 101 [100, 102] | 61.63 [60.73, 62.44] | loss | 1.01 [0.99, 1.02] |
| total browser memory p50 (MB) | 357 [349, 361] | 5919 [5749, 6020] | 365 [360, 366] | 16.60 [16.00, 17.08] | loss | 1.02 [1.00, 1.05] |

### prismatic_fluids

- chrome: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_dawn: 12/12 ok, Doe mapped {'False': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- fawn_doe: 12/12 ok, Doe mapped {'True': 12}, adapter amd / rdna-2 /  / , pixel hashes 12
- identical pixel hashes across arms: False

| metric | chrome | fawn_doe | fawn_dawn | fawn_doe/chrome (95% CI) | verdict | fawn_dawn/chrome (95% CI) |
|---|---|---|---|---|---|---|
| cold first job (nav -> first completed job) p50 (ms) | 646 [627, 1046] | 820 [812, 824] | 647 [634, 653] | 1.27 [0.78, 1.31] | no clear difference | 1.00 [0.61, 1.04] |
| cold first job (nav -> first completed job) p95 (ms) | 1396 [1046, 1396] | 843 [824, 843] | 668 [653, 668] | 0.60 [0.59, 0.80] | win | 0.48 [0.47, 0.63] |
| launch -> first completed job p50 (ms) | 1964 [958, 2480] | 1186 [1170, 1297] | 1002 [998, 1118] | 0.60 [0.47, 1.25] | no clear difference | 0.51 [0.40, 1.12] |
| frame/op completion p50 p50 (ms) | 3.90 [3.90, 3.90] | 6.40 [5.90, 6.70] | 3.90 [3.90, 3.90] | 1.64 [1.51, 1.72] | loss | 1.00 [1.00, 1.00] |
| frame/op completion p95 p50 (ms) | 5.90 [5.00, 5.90] | 10.3 [9.90, 10.4] | 5.00 [4.90, 5.00] | 1.75 [1.68, 2.06] | loss | 0.85 [0.83, 1.00] |
| frame/op completion p99 p50 (ms) | 6.00 [6.00, 6.00] | 20.0 [17.6, 22.5] | 6.00 [5.90, 6.00] | 3.33 [2.93, 3.75] | loss | 1.00 [0.98, 1.00] |
| completed-frame interval p50 p50 (ms) | 9.40 [9.30, 9.50] | 10.3 [10.2, 10.3] | 9.20 [9.20, 9.20] | 1.10 [1.08, 1.11] | loss | 0.98 [0.97, 0.99] |
| completed-frame interval p95 p50 (ms) | 11.1 [11.1, 11.1] | 14.3 [14.1, 14.4] | 11.1 [11.1, 11.1] | 1.29 [1.27, 1.30] | loss | 1.00 [1.00, 1.00] |
| completed-frame interval p99 p50 (ms) | 12.1 [12.0, 12.1] | 27.9 [24.6, 30.2] | 11.9 [11.8, 12.2] | 2.31 [2.03, 2.50] | loss | 0.98 [0.98, 1.01] |
| GPU-process CPU per frame/op p50 (ms) | 1.70 [1.67, 1.71] | 5.06 [5.03, 5.08] | 2.30 [2.27, 2.32] | 2.97 [2.94, 3.04] | loss | 1.35 [1.33, 1.39] |
| all-process CPU per frame/op p50 (ms) | 2.50 [2.45, 2.52] | 6.82 [6.79, 6.98] | 3.71 [3.69, 3.73] | 2.73 [2.70, 2.84] | loss | 1.48 [1.47, 1.52] |
| GPU-process memory p50 (MB) | 194 [192, 197] | 5472 [5425, 5571] | 198 [198, 200] | 28.15 [27.67, 28.88] | loss | 1.02 [1.01, 1.04] |
| GPU-process graphics memory p50 (MB) | 106 [106, 108] | 6012 [5967, 6069] | 108 [108, 108] | 56.68 [55.31, 57.21] | loss | 1.02 [1.00, 1.02] |
| total browser memory p50 (MB) | 370 [368, 374] | 5647 [5600, 5753] | 373 [369, 375] | 15.25 [15.08, 15.58] | loss | 1.01 [0.99, 1.02] |
