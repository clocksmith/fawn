@group(0) @binding(0) var<storage, read_write> arguments: array<u32>;
@compute @workgroup_size(1)
fn main() {
    arguments[0] = 0xa5a5a5a5u;
    arguments[1] = 2u; arguments[2] = 1u; arguments[3] = 1u;
    arguments[4] = 0xa5a5a5a5u;
    arguments[5] = 3u; arguments[6] = 1u; arguments[7] = 1u;
    arguments[8] = 0xa5a5a5a5u;
    arguments[9] = 1u; arguments[10] = 4u; arguments[11] = 1u;
    arguments[12] = 0xa5a5a5a5u;
    arguments[13] = 1u; arguments[14] = 1u; arguments[15] = 5u;
    arguments[16] = 0xa5a5a5a5u;
    arguments[17] = 0u; arguments[18] = 1u; arguments[19] = 1u;
}
