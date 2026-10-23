@group(0) @binding(0) var<storage, read_write> signed_cells: array<atomic<i32>>;
@group(0) @binding(1) var<storage, read_write> unsigned_cells: array<atomic<u32>>;
@group(0) @binding(2) var<storage, read_write> results: array<u32>;
var<workgroup> signed_shared: array<atomic<i32>, 4>;
var<workgroup> unsigned_shared: array<atomic<u32>, 4>;
@compute @workgroup_size(6)
fn main(@builtin(local_invocation_index) lane: u32) {
    let index = i32(lane) - 1;
    let base = lane * 56u;
    {
        results[base + 0u] = u32(atomicLoad(&signed_cells[index]));
        atomicStore(&signed_cells[index], -7i);
        results[base + 1u] = u32(atomicAdd(&signed_cells[index], 3i));
        results[base + 2u] = u32(atomicSub(&signed_cells[index], 2i));
        results[base + 3u] = u32(atomicMax(&signed_cells[index], 5i));
        results[base + 4u] = u32(atomicMin(&signed_cells[index], -5i));
        results[base + 5u] = u32(atomicAnd(&signed_cells[index], 3i));
        results[base + 6u] = u32(atomicOr(&signed_cells[index], 4i));
        results[base + 7u] = u32(atomicXor(&signed_cells[index], 6i));
        results[base + 8u] = u32(atomicExchange(&signed_cells[index], 9i));
        let success = atomicCompareExchangeWeak(&signed_cells[index], 9i, 11i);
        results[base + 9u] = u32(success.old_value);
        results[base + 10u] = u32(success.exchanged);
        let failure = atomicCompareExchangeWeak(&signed_cells[index], 0i, 17i);
        results[base + 11u] = u32(failure.old_value);
        results[base + 12u] = u32(failure.exchanged);
        results[base + 13u] = u32(atomicLoad(&signed_cells[index]));
    }
    {
        results[base + 14u] = u32(atomicLoad(&unsigned_cells[index]));
        atomicStore(&unsigned_cells[index], 4294967293u);
        results[base + 15u] = u32(atomicAdd(&unsigned_cells[index], 5u));
        results[base + 16u] = u32(atomicSub(&unsigned_cells[index], 3u));
        results[base + 17u] = u32(atomicMax(&unsigned_cells[index], 7u));
        results[base + 18u] = u32(atomicMin(&unsigned_cells[index], 9u));
        results[base + 19u] = u32(atomicAnd(&unsigned_cells[index], 6u));
        results[base + 20u] = u32(atomicOr(&unsigned_cells[index], 3u));
        results[base + 21u] = u32(atomicXor(&unsigned_cells[index], 6u));
        results[base + 22u] = u32(atomicExchange(&unsigned_cells[index], 9u));
        let success = atomicCompareExchangeWeak(&unsigned_cells[index], 9u, 11u);
        results[base + 23u] = u32(success.old_value);
        results[base + 24u] = u32(success.exchanged);
        let failure = atomicCompareExchangeWeak(&unsigned_cells[index], 0u, 17u);
        results[base + 25u] = u32(failure.old_value);
        results[base + 26u] = u32(failure.exchanged);
        results[base + 27u] = u32(atomicLoad(&unsigned_cells[index]));
    }
    {
        results[base + 28u] = u32(atomicLoad(&signed_shared[index]));
        atomicStore(&signed_shared[index], -7i);
        results[base + 29u] = u32(atomicAdd(&signed_shared[index], 3i));
        results[base + 30u] = u32(atomicSub(&signed_shared[index], 2i));
        results[base + 31u] = u32(atomicMax(&signed_shared[index], 5i));
        results[base + 32u] = u32(atomicMin(&signed_shared[index], -5i));
        results[base + 33u] = u32(atomicAnd(&signed_shared[index], 3i));
        results[base + 34u] = u32(atomicOr(&signed_shared[index], 4i));
        results[base + 35u] = u32(atomicXor(&signed_shared[index], 6i));
        results[base + 36u] = u32(atomicExchange(&signed_shared[index], 9i));
        let success = atomicCompareExchangeWeak(&signed_shared[index], 9i, 11i);
        results[base + 37u] = u32(success.old_value);
        results[base + 38u] = u32(success.exchanged);
        let failure = atomicCompareExchangeWeak(&signed_shared[index], 0i, 17i);
        results[base + 39u] = u32(failure.old_value);
        results[base + 40u] = u32(failure.exchanged);
        results[base + 41u] = u32(atomicLoad(&signed_shared[index]));
    }
    {
        results[base + 42u] = u32(atomicLoad(&unsigned_shared[index]));
        atomicStore(&unsigned_shared[index], 4294967293u);
        results[base + 43u] = u32(atomicAdd(&unsigned_shared[index], 5u));
        results[base + 44u] = u32(atomicSub(&unsigned_shared[index], 3u));
        results[base + 45u] = u32(atomicMax(&unsigned_shared[index], 7u));
        results[base + 46u] = u32(atomicMin(&unsigned_shared[index], 9u));
        results[base + 47u] = u32(atomicAnd(&unsigned_shared[index], 6u));
        results[base + 48u] = u32(atomicOr(&unsigned_shared[index], 3u));
        results[base + 49u] = u32(atomicXor(&unsigned_shared[index], 6u));
        results[base + 50u] = u32(atomicExchange(&unsigned_shared[index], 9u));
        let success = atomicCompareExchangeWeak(&unsigned_shared[index], 9u, 11u);
        results[base + 51u] = u32(success.old_value);
        results[base + 52u] = u32(success.exchanged);
        let failure = atomicCompareExchangeWeak(&unsigned_shared[index], 0u, 17u);
        results[base + 53u] = u32(failure.old_value);
        results[base + 54u] = u32(failure.exchanged);
        results[base + 55u] = u32(atomicLoad(&unsigned_shared[index]));
    }
}
