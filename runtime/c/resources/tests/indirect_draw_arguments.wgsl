@group(0) @binding(0) var<storage, read_write> arguments: array<u32>;
@compute @workgroup_size(1)
fn main() {
    arguments[0] = 0xa5a5a5a5u;
    arguments[1] = 3u;
    arguments[2] = 1u;
    arguments[3] = 0u;
    arguments[4] = 0u;
    arguments[5] = 0xa5a5a5a5u;
    arguments[6] = 3u;
    arguments[7] = 1u;
    arguments[8] = 4294967295u;
    arguments[9] = 0u;
    arguments[10] = 0xa5a5a5a5u;
    arguments[11] = 3u;
    arguments[12] = 2u;
    arguments[13] = 0u;
    arguments[14] = 0u;
    arguments[15] = 0xa5a5a5a5u;
    arguments[16] = 3u;
    arguments[17] = 1u;
    arguments[18] = 0u;
    arguments[19] = 1u;
    arguments[20] = 0xa5a5a5a5u;
    arguments[21] = 0u;
    arguments[22] = 1u;
    arguments[23] = 0u;
    arguments[24] = 0u;
    arguments[25] = 0xa5a5a5a5u;
    arguments[26] = 6u;
    arguments[27] = 1u;
    arguments[28] = 0u;
    arguments[29] = 0u;
}
