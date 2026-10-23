@group(0) @binding(0) var<storage, read_write> counter: array<atomic<u32>>;
@compute @workgroup_size(1)
fn main() {
    atomicAdd(&counter[0], 1u);
}
